/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                       S C A N D E V . C P P

GENERAL DESCRIPTION
    This file implements device scanning, enumeration, and monitoring
    functions for Qualcomm USB devices on Windows.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#include "scandev.h"
#include "utils.h"

using namespace QcDevice;

namespace QcDevice
{
    namespace
    {
        PVOID CallerContext = NULL;
        INTERNAL_DEV_FEATURE_SETTING FeatureSetting;
        BOOL                  bFeatureSet = FALSE;
        DEVICECHANGE_CALLBACK fNotifyCb = NULL;
        DEVICECHANGE_CALLBACK_N fNotifyCb_N = NULL;
        QCD_LOGGING_CALLBACK  fLogger = NULL;
        BOOL                  bMonitorRunning = FALSE;
        HANDLE hStopMonitorEvt, hMonitorStartedEvt;
        HANDLE hAnnouncementEvt, hAnnouncementExitEvt;
        HANDLE hMonitorThread, hAnnouncementThread = 0;
        HANDLE hRegChangeEvt[QC_REG_MAX];
        HKEY hSysKey[QC_REG_MAX];
        LONG lInOperation = 0;
        LONG lInitialized = 0;
        CRITICAL_SECTION opLock, notifyLock;
        LIST_ENTRY ArrivalList, FreeList, NewArrivalList;
        LIST_ENTRY NotifyFreeList, AnnouncementList;
        PQC_NOTIFICATION_STORE NotifyStore;

        VOID InitializeLists(VOID)
        {
            int i;
            PQC_DEV_ITEM pItem;

            InitializeListHead(&ArrivalList);
            InitializeListHead(&FreeList);
            InitializeListHead(&NewArrivalList);
            InitializeListHead(&NotifyFreeList);
            InitializeListHead(&AnnouncementList);
            for (i = 0; i < MAX_NUM_DEV; i++)
            {
                pItem = (PQC_DEV_ITEM)malloc(sizeof(QC_DEV_ITEM));
                if (pItem == NULL)
                {
                    break;
                }
                pItem->Info.Type = QC_DEV_TYPE_NONE;
                pItem->Info.Flag = DEV_FLAG_NONE;
                pItem->Info.IsQCDriver = 0;
                pItem->UserContext = NULL;
                ZeroMemory(pItem->DevDesc, QC_MAX_VALUE_NAME);
                ZeroMemory(pItem->DevDescA, QC_MAX_VALUE_NAME);
                ZeroMemory(pItem->DevNameW, QC_MAX_VALUE_NAME);
                ZeroMemory(pItem->DevNameA, QC_MAX_VALUE_NAME);
                ZeroMemory(pItem->HwId, QC_MAX_VALUE_NAME);
                ZeroMemory(pItem->ParentDev, QC_MAX_VALUE_NAME);
                ZeroMemory(pItem->InterfaceName, QC_MAX_VALUE_NAME);
                ZeroMemory(pItem->SerNum, 256);
                ZeroMemory(pItem->SerNumMsm, 256);
                InsertTailList(&FreeList, &(pItem->List));
            }
            NotifyStore = (PQC_NOTIFICATION_STORE)malloc(sizeof(QC_NOTIFICATION_STORE) * 512);
            if (NotifyStore == NULL)
            {
                QCD_Printf("Failed to alloc notification store\n");
            }
            else
            {
                for (i = 0; i < 512; i++)
                {
                    InitializeListHead(&(NotifyStore[i].DevItemChain));
                    InsertTailList(&NotifyFreeList, &(NotifyStore[i].List));
                }
            }
        }  // InitializeLists

        VOID CleanupList(PLIST_ENTRY ItemList)
        {
            PLIST_ENTRY pEntry;
            PQC_DEV_ITEM pItem;

            EnterCriticalSection(&opLock);
            while (!IsListEmpty(ItemList))
            {
                pEntry = RemoveHeadList(ItemList);
                pItem = CONTAINING_RECORD(pEntry, QC_DEV_ITEM, List);
                free(pItem);
            }
            LeaveCriticalSection(&opLock);
        }  // CleanupList

        VOID TryToAnnounce(PLIST_ENTRY ActiveList, PLIST_ENTRY NewArrival)
        {
            PQC_DEV_ITEM pActive, pArrival;
            PLIST_ENTRY headOfActive, peekActive;
            PLIST_ENTRY headOfArrival, peekArrival;
            PLIST_ENTRY headOfStore;
            PQC_NOTIFICATION_STORE storeBranch = NULL;

            // NOTE: ActiveList is the ArrivalList

            EnterCriticalSection(&opLock);

            if ((!IsListEmpty(ActiveList)) && (!IsListEmpty(NewArrival)))
            {
                headOfActive = ActiveList;
                peekActive = headOfActive->Flink;
                while (peekActive != headOfActive)
                {
                    pActive = CONTAINING_RECORD(peekActive, QC_DEV_ITEM, List);
                    peekActive = peekActive->Flink;

                    headOfArrival = NewArrival;
                    peekArrival = headOfArrival->Flink;
                    while (peekArrival != headOfArrival)
                    {
                        pArrival = CONTAINING_RECORD(peekArrival, QC_DEV_ITEM, List);
                        peekArrival = peekArrival->Flink;
                        if ((StrCmp((LPCWSTR)pActive->DevDesc, (LPCWSTR)pArrival->DevDesc) == 0) &&
                            (StrCmp((LPCWSTR)pActive->DevNameW, (LPCWSTR)pArrival->DevNameW) == 0))
                        {
                            if (StrCmp((LPCWSTR)pActive->SerNum, (LPCWSTR)pArrival->SerNum) == 0)
                            {
                                pActive->Info.Flag = pArrival->Info.Flag = DEV_FLAG_ARRIVAL;
                            }
                        }
                    }  // while
                }  // while
            }  // if

            // Get a storeBranch for devices to be announced
            {
                EnterCriticalSection(&notifyLock);
                if (!IsListEmpty(&NotifyFreeList))
                {
                    headOfStore = RemoveHeadList(&NotifyFreeList);
                    storeBranch = CONTAINING_RECORD(headOfStore, QC_NOTIFICATION_STORE, List);
                }
                LeaveCriticalSection(&notifyLock);
            }

            // New arrivals are Flag==0 on NewArrival
            while (!IsListEmpty(NewArrival))
            {
                headOfArrival = RemoveHeadList(NewArrival);
                pArrival = CONTAINING_RECORD(headOfArrival, QC_DEV_ITEM, List);
                if (pArrival->Info.Flag == DEV_FLAG_NONE)
                {
                    PQC_DEV_ITEM pDevice;

                    pDevice = (PQC_DEV_ITEM)malloc(sizeof(QC_DEV_ITEM));
                    pArrival->Info.Flag = DEV_FLAG_ARRIVAL;
                    if ((storeBranch == NULL) || (hAnnouncementThread == 0) || (pDevice == NULL))
                    {
                        if (fNotifyCb != NULL)
                        {
                            pArrival->CbParams.DevDesc = (PWCHAR)pArrival->DevDesc;
                            pArrival->CbParams.DevName = (PCHAR)pArrival->DevNameA;
                            pArrival->CbParams.IfName = (PWCHAR)pArrival->InterfaceName;
                            pArrival->CbParams.Loc = (PWCHAR)pArrival->Location;
                            pArrival->CbParams.DevPath = (PWCHAR)pArrival->DevPath;
                            pArrival->CbParams.SerNum = (PWCHAR)pArrival->SerNum;
                            pArrival->CbParams.SerNumMsm = (PWCHAR)pArrival->SerNumMsm;
                            pArrival->CbParams.HwId = (PWCHAR)pArrival->HwId;
                            pArrival->CbParams.ParentDev = (PWCHAR)pArrival->ParentDev;
                            pArrival->CbParams.Flag = ((ULONG)pArrival->Info.Type << 8) |
                                ((ULONG)1 << 4) |
                                ((ULONG)pArrival->BusType << 16) |
                                (ULONG)pArrival->Info.IsQCDriver;
                            if (CallerContext != NULL)
                            {
                                pArrival->UserContext = CallerContext;
                            }
                            fNotifyCb(&(pArrival->CbParams), &pArrival->UserContext);
                            QCD_Printf("   HWID [%ws]\n", pArrival->HwId);
                        }
                    }
                    else  // send to announcement thread
                    {
                        CopyMemory((PVOID)pDevice, (PVOID)pArrival, sizeof(QC_DEV_ITEM));
                        pDevice->Context = (PVOID)pArrival;
                        InsertTailList(&(storeBranch->DevItemChain), &pDevice->List);
                    }
                    InsertTailList(&ArrivalList, &pArrival->List); // ArrivalList is ActiveList
                }
                else
                {
                    ZeroMemory(pArrival, sizeof(QC_DEV_ITEM));
                    InsertTailList(&FreeList, &pArrival->List);
                }
            }  // while

            // Those departed are Intersection==0 on ActiveList
            // Now, this list also has new arrivals marked with DEV_FLAG_ARRIVAL
            if (!IsListEmpty(ActiveList))
            {
                headOfActive = ActiveList;
                peekActive = headOfActive->Flink;
                while (peekActive != headOfActive)
                {
                    pActive = CONTAINING_RECORD(peekActive, QC_DEV_ITEM, List);
                    peekActive = peekActive->Flink;
                    if (pActive->Info.Flag == DEV_FLAG_NONE)
                    {
                        PQC_DEV_ITEM pDevice;

                        pDevice = (PQC_DEV_ITEM)malloc(sizeof(QC_DEV_ITEM));
                        RemoveEntryList(&pActive->List);
                        if ((storeBranch == NULL) || (hAnnouncementThread == 0) || (pDevice == NULL))
                        {
                            if (fNotifyCb != NULL)
                            {
                                pActive->CbParams.DevDesc = (PWCHAR)pActive->DevDesc;
                                pActive->CbParams.DevName = (PCHAR)pActive->DevNameA;
                                pActive->CbParams.IfName = (PWCHAR)pActive->InterfaceName;
                                pActive->CbParams.Loc = (PWCHAR)pActive->Location;
                                pActive->CbParams.DevPath = (PWCHAR)pActive->DevPath;
                                pActive->CbParams.SerNum = (PWCHAR)pActive->SerNum;
                                pActive->CbParams.SerNumMsm = (PWCHAR)pActive->SerNumMsm;
                                pActive->CbParams.HwId = (PWCHAR)pActive->HwId;
                                pActive->CbParams.ParentDev = (PWCHAR)pActive->ParentDev;
                                pActive->CbParams.Flag = ((ULONG)pActive->Info.Type << 8) |
                                    ((ULONG)pActive->BusType << 16) |
                                    pActive->Info.IsQCDriver;

                                if (CallerContext != NULL)
                                {
                                    pActive->UserContext = CallerContext;
                                }
                                fNotifyCb
                                (
                                    &(pActive->CbParams),
                                    &pActive->UserContext
                                );
                                QCD_Printf("   HWID [%ws]\n", pActive->HwId);
                            }
                        }
                        else
                        {
                            pActive->Info.Flag = DEV_FLAG_DEPARTURE;
                            CopyMemory((PVOID)pDevice, (PVOID)pActive, sizeof(QC_DEV_ITEM));
                            InsertTailList(&(storeBranch->DevItemChain), &pDevice->List);
                        }
                        ZeroMemory(pActive, sizeof(QC_DEV_ITEM));
                        InsertTailList(&FreeList, &pActive->List);
                    }
                    else
                    {
                        pActive->Info.Flag = DEV_FLAG_NONE;  // reset
                    }
                }  // while
            }  // if

            LeaveCriticalSection(&opLock);

            // put records to NotifyStore
            if (storeBranch != NULL)
            {
                EnterCriticalSection(&notifyLock);
                if (!IsListEmpty(&(storeBranch->DevItemChain)))
                {
                    InsertTailList(&AnnouncementList, &(storeBranch->List));
                    SetEvent(hAnnouncementEvt);
                }
                else
                {
                    InsertHeadList(&NotifyFreeList, &(storeBranch->List));
                }
                LeaveCriticalSection(&notifyLock);
            }
        }  // TryToAnnounce

        VOID MakeAnnouncement(VOID)
        {
            PQC_NOTIFICATION_STORE storeBranch;
            PQC_DEV_ITEM devItem;
            PLIST_ENTRY headOfList;

            QCD_Printf("Announcing device:\n");
            EnterCriticalSection(&notifyLock);
            while (!IsListEmpty(&AnnouncementList))
            {
                headOfList = RemoveHeadList(&AnnouncementList);
                storeBranch = CONTAINING_RECORD(headOfList, QC_NOTIFICATION_STORE, List);
                LeaveCriticalSection(&notifyLock);

                while (!IsListEmpty(&(storeBranch->DevItemChain)))
                {
                    headOfList = RemoveHeadList(&(storeBranch->DevItemChain));
                    devItem = CONTAINING_RECORD(headOfList, QC_DEV_ITEM, List);
                    if (fNotifyCb != NULL)
                    {
                        if (devItem->Info.Flag == DEV_FLAG_ARRIVAL)
                        {
                            PQC_DEV_ITEM context = (PQC_DEV_ITEM)(devItem->Context);

                            devItem->CbParams.DevDesc = (PWCHAR)devItem->DevDesc;
                            devItem->CbParams.DevName = (PCHAR)devItem->DevNameA;
                            devItem->CbParams.IfName = (PWCHAR)devItem->InterfaceName;
                            devItem->CbParams.Loc = (PWCHAR)devItem->Location;
                            devItem->CbParams.DevPath = (PWCHAR)devItem->DevPath;
                            devItem->CbParams.SerNum = (PWCHAR)devItem->SerNum;
                            devItem->CbParams.SerNumMsm = (PWCHAR)devItem->SerNumMsm;
                            devItem->CbParams.HwId = (PWCHAR)devItem->HwId;
                            devItem->CbParams.ParentDev = (PWCHAR)devItem->ParentDev;
                            devItem->CbParams.Flag = ((ULONG)devItem->Info.Type << 8) |
                                ((ULONG)devItem->BusType << 16) |
                                ((ULONG)1 << 4) |
                                (ULONG)devItem->Info.IsQCDriver;
                            QCD_Printf("ARRIVAL: <%ws> pItem 0x%p - 0x%p - 0x%p\n", (PWCHAR)devItem->DevDesc,
                                       devItem, devItem->Context, &(devItem->CbParams));
                            if (CallerContext != NULL)
                            {
                                devItem->UserContext = CallerContext;
                            }
                            fNotifyCb
                            (
                                &(devItem->CbParams),
                                &devItem->UserContext
                            );
                            context->UserContext = devItem->UserContext; // save user context ID
                            QCD_Printf("   HWID [%ws]\n", devItem->HwId);
                        }
                        else  // DEV_FLAG_DEPARTURE
                        {
                            devItem->CbParams.DevDesc = (PWCHAR)devItem->DevDesc;
                            devItem->CbParams.DevName = (PCHAR)devItem->DevNameA;
                            devItem->CbParams.IfName = (PWCHAR)devItem->InterfaceName;
                            devItem->CbParams.Loc = (PWCHAR)devItem->Location;
                            devItem->CbParams.DevPath = (PWCHAR)devItem->DevPath;
                            devItem->CbParams.SerNum = (PWCHAR)devItem->SerNum;
                            devItem->CbParams.SerNumMsm = (PWCHAR)devItem->SerNumMsm;
                            devItem->CbParams.HwId = (PWCHAR)devItem->HwId;
                            devItem->CbParams.ParentDev = (PWCHAR)devItem->ParentDev;
                            devItem->CbParams.Flag = ((ULONG)devItem->Info.Type << 8) |
                                ((ULONG)devItem->BusType << 16) |
                                (ULONG)devItem->Info.IsQCDriver;
                            QCD_Printf("DEPARTURE: <%ws>\n", (PWCHAR)devItem->DevDesc);

                            if (CallerContext != NULL)
                            {
                                devItem->UserContext = CallerContext;
                            }
                            fNotifyCb
                            (
                                &(devItem->CbParams),
                                &devItem->UserContext
                            );
                            QCD_Printf("   HWID [%ws]\n", devItem->HwId);
                        }
                    }
                    free(devItem);
                }
                EnterCriticalSection(&notifyLock);
                InsertTailList(&NotifyFreeList, &(storeBranch->List)); // recycle
            }
            LeaveCriticalSection(&notifyLock);
        }  // MakeAnnouncement

        VOID MakeAnnouncement_N(VOID)
        {
            PQC_NOTIFICATION_STORE storeBranch;
            PQC_DEV_ITEM devItem;
            PLIST_ENTRY headOfList;

            QCD_Printf("Announcing device:\n");
            EnterCriticalSection(&notifyLock);
            while (!IsListEmpty(&AnnouncementList))
            {
                headOfList = RemoveHeadList(&AnnouncementList);
                storeBranch = CONTAINING_RECORD(headOfList, QC_NOTIFICATION_STORE, List);
                LeaveCriticalSection(&notifyLock);

                while (!IsListEmpty(&(storeBranch->DevItemChain)))
                {
                    headOfList = RemoveHeadList(&(storeBranch->DevItemChain));
                    devItem = CONTAINING_RECORD(headOfList, QC_DEV_ITEM, List);
                    if (fNotifyCb != NULL)
                    {
                        if (devItem->Info.Flag == DEV_FLAG_ARRIVAL)
                        {
                            PQC_DEV_ITEM context = (PQC_DEV_ITEM)(devItem->Context);

                            devItem->CbParams.DevDesc = (PWCHAR)devItem->DevDesc;
                            devItem->CbParams.DevName = (PCHAR)devItem->DevNameA;
                            devItem->CbParams.IfName = (PWCHAR)devItem->InterfaceName;
                            devItem->CbParams.Loc = (PWCHAR)devItem->Location;
                            devItem->CbParams.DevPath = (PWCHAR)devItem->DevPath;
                            devItem->CbParams.SerNum = (PWCHAR)devItem->SerNum;
                            devItem->CbParams.SerNumMsm = (PWCHAR)devItem->SerNumMsm;
                            devItem->CbParams.HwId = (PWCHAR)devItem->HwId;
                            devItem->CbParams.ParentDev = (PWCHAR)devItem->ParentDev;
                            devItem->CbParams.Flag = (((ULONG)devItem->Info.Type << 8) |
                                                      ((ULONG)1 << 4) |
                                                      (ULONG)devItem->Info.IsQCDriver);
                            QCD_Printf("ARRIVAL: <%ws>\n", (PWCHAR)devItem->DevDesc);
                            if (CallerContext != NULL)
                            {
                                devItem->UserContext = CallerContext;
                            }
                            fNotifyCb
                            (
                                &(devItem->CbParams),
                                &devItem->UserContext
                            );
                            context->UserContext = devItem->UserContext; // save user context ID
                            QCD_Printf("   HWID [%ws]\n", devItem->HwId);
                        }
                        else  // DEV_FLAG_DEPARTURE
                        {
                            devItem->CbParams.DevDesc = (PWCHAR)devItem->DevDesc;
                            devItem->CbParams.DevName = (PCHAR)devItem->DevNameA;
                            devItem->CbParams.IfName = (PWCHAR)devItem->InterfaceName;
                            devItem->CbParams.Loc = (PWCHAR)devItem->Location;
                            devItem->CbParams.DevPath = (PWCHAR)devItem->DevPath;
                            devItem->CbParams.SerNum = (PWCHAR)devItem->SerNum;
                            devItem->CbParams.SerNumMsm = (PWCHAR)devItem->SerNumMsm;
                            devItem->CbParams.HwId = (PWCHAR)devItem->HwId;
                            devItem->CbParams.ParentDev = (PWCHAR)devItem->ParentDev;
                            devItem->CbParams.Flag = (((ULONG)devItem->Info.Type << 8) | (ULONG)devItem->Info.IsQCDriver);
                            QCD_Printf("DEPARTURE: <%ws>\n", (PWCHAR)devItem->DevDesc);
                            if (CallerContext != NULL)
                            {
                                devItem->UserContext = CallerContext;
                            }
                            fNotifyCb
                            (
                                &(devItem->CbParams),
                                &devItem->UserContext
                            );
                            QCD_Printf("   HWID [%ws]\n", devItem->HwId);
                        }
                    }
                    free(devItem);
                }
                EnterCriticalSection(&notifyLock);
                InsertTailList(&NotifyFreeList, &(storeBranch->List)); // recycle
            }
            LeaveCriticalSection(&notifyLock);
        }  // MakeAnnouncement_N

        DWORD WINAPI AnnouncementThread(PVOID Context)
        {
            DWORD status = WAIT_OBJECT_0;

            while (bMonitorRunning == TRUE)  // set in StartDeviceMonitor()
            {
                status = WaitForSingleObject(hAnnouncementEvt, 500);
                if (status == WAIT_OBJECT_0)
                {
                    if (fNotifyCb_N != NULL)
                    {
                        fNotifyCb_N();
                    }
                    else
                    {
                        MakeAnnouncement();
                    }
                }
            }
            SetEvent(hAnnouncementExitEvt);
            return 0;
        }  // AnnouncementThread

        BOOL MonitorDeviceChange(VOID)
        {
            static WCHAR keyPath[REG_HW_ID_SIZE];
            LONG entries, result;
            static DWORD status = WAIT_OBJECT_0;
            DWORD retCode;

            // QCD_Printf("-->MonitorDeviceChange\n");

            // Make sure events are created only once
            if ((entries = InterlockedIncrement(&lInitialized)) == 1)
            {
                int i;

                for (i = QC_REG_DEVMAP; i < QC_REG_MAX; i++)
                {
                    hRegChangeEvt[i] = CreateEvent(NULL, FALSE, FALSE, NULL);
                    if (hRegChangeEvt[i] == NULL)
                    {
                        QCD_Printf("CreateEvent failed: 0x%x\n", GetLastError());
                    }
                    else if (i == QC_REG_DEVMAP)
                    {
                        // trigger an immediate 1st-time scan later in this function
                        SetEvent(hRegChangeEvt[i]);
                    }
                }

                // not useful because ADB driver does not make reg change on arrival/departure
                StringCchCopy(keyPath, MAX_PATH, TEXT(QC_REG_SW_KEY_ADB));
                retCode = RegOpenKeyEx
                (
                    HKEY_LOCAL_MACHINE,
                    keyPath,
                    0,
                    KEY_READ,
                    &hSysKey[QC_REG_ADB]
                );
                if (retCode != ERROR_SUCCESS)
                {
                    hSysKey[QC_REG_ADB] = 0;
                    QCD_Printf("Error: failed to open reg for SW_ADB\n");
                }

                StringCchCopy(keyPath, MAX_PATH, TEXT(QC_REG_SW_KEY_USB));
                retCode = RegOpenKeyEx
                (
                    HKEY_LOCAL_MACHINE,
                    keyPath,
                    0,
                    KEY_READ,
                    &hSysKey[QC_REG_USB]
                );
                if (retCode != ERROR_SUCCESS)
                {
                    hSysKey[QC_REG_USB] = 0;
                    QCD_Printf("Error: failed to open reg for SW_USB\n");
                }

                StringCchCopy(keyPath, MAX_PATH, TEXT(QC_REG_KEY_DEVMAP));
                retCode = RegOpenKeyEx
                (
                    HKEY_LOCAL_MACHINE,
                    keyPath,
                    0,
                    KEY_READ,
                    &hSysKey[QC_REG_DEVMAP]
                );
                if (retCode != ERROR_SUCCESS)
                {
                    hSysKey[QC_REG_DEVMAP] = 0;
                    QCD_Printf("Error: failed to open reg for DEVMAP\n");
                }

                StringCchCopy(keyPath, MAX_PATH, TEXT(QC_REG_SW_KEY_MDM));
                retCode = RegOpenKeyEx
                (
                    HKEY_LOCAL_MACHINE,
                    keyPath,
                    0,
                    KEY_READ,
                    &hSysKey[QC_REG_MDM]
                );
                if (retCode != ERROR_SUCCESS)
                {
                    hSysKey[QC_REG_MDM] = 0;
                    QCD_Printf("Error: failed to open reg for MDM\n");
                }

                StringCchCopy(keyPath, MAX_PATH, TEXT(QC_REG_SW_KEY_NET));
                retCode = RegOpenKeyEx
                (
                    HKEY_LOCAL_MACHINE,
                    keyPath,
                    0,
                    KEY_READ,
                    &hSysKey[QC_REG_NET]
                );
                if (retCode != ERROR_SUCCESS)
                {
                    hSysKey[QC_REG_NET] = 0;
                    QCD_Printf("Error: failed to open reg for NET\n");
                }

                StringCchCopy(keyPath, MAX_PATH, TEXT(QC_REG_SW_KEY_PORTS));
                retCode = RegOpenKeyEx
                (
                    HKEY_LOCAL_MACHINE,
                    keyPath,
                    0,
                    KEY_READ,
                    &hSysKey[QC_REG_PORTS]
                );
                if (retCode != ERROR_SUCCESS)
                {
                    hSysKey[QC_REG_PORTS] = 0;
                    QCD_Printf("Error: failed to open reg for PORTS\n");
                }

            }
            else if (entries > 1)
            {
                InterlockedDecrement(&lInitialized);
            }

            if (status != WAIT_TIMEOUT)
            {
                int i;

                for (i = QC_REG_DEVMAP; i < QC_REG_MAX; i++)
                {
                    if (hSysKey[i] != 0)
                    {
                        result = RegNotifyChangeKeyValue
                        (
                            hSysKey[i],
                            TRUE,
                            REG_NOTIFY_CHANGE_LAST_SET,
                            hRegChangeEvt[i],
                            TRUE
                        );
                        if (result != ERROR_SUCCESS)
                        {
                            QCD_Printf("Failed to monitor reg location %d - 0x%x\n", i, GetLastError());
                        }
                    }
                    else
                    {
                        QCD_Printf("Skip to monitor reg location %d\n", i);
                    }
                }
                status = WaitForMultipleObjects(QC_REG_MAX, hRegChangeEvt, FALSE, FeatureSetting.TimerInterval);
                // QCD_Printf("Reg change detected: 0x%x\n", status);
            }
            else
            {
                SetEvent(hRegChangeEvt[0]);  // scan on timer
                status = WaitForMultipleObjects(QC_REG_MAX, hRegChangeEvt, FALSE, FeatureSetting.TimerInterval);
                // QCD_Printf("timer off: 0x%x\n", status);
            }

            // QCD_Printf("<--MonitorDeviceChange: ST 0x%x\n", status);

            return status; // WAIT_OBJECT_0);

        }  // MonitorDeviceChange

        PBYTE GetPortNameFromHwKey(PTSTR LocationPath)
        {
            WCHAR keyPath[REG_HW_ID_SIZE];
            static BYTE  regValue[QC_MAX_VALUE_NAME];
            DWORD regLen = QC_MAX_VALUE_NAME;
            HKEY  hHwKey;
            DWORD retCode;

            StringCchCopy(keyPath, MAX_PATH, TEXT(QC_REG_HW_KEY_PREF));
            StringCchCat(keyPath, MAX_PATH, LocationPath);
            StringCchCat(keyPath, MAX_PATH, TEXT(QC_REG_HW_KEY_PARAM));

            retCode = RegOpenKeyEx
            (
                HKEY_LOCAL_MACHINE,
                keyPath,
                0,
                KEY_READ,
                &hHwKey
            );

            if (retCode == ERROR_SUCCESS)
            {
                retCode = RegQueryValueEx
                (
                    hHwKey,
                    TEXT(DEV_PORT_NAME),
                    NULL,         // reserved
                    NULL,         // returned type
                    (LPBYTE)regValue,
                    &regLen
                );
                RegCloseKey(hHwKey);

                if (retCode == ERROR_SUCCESS)
                {
                    return (PBYTE)regValue;
                }
            }
            return NULL;
        }  // GetPortNameFromHwKey

        VOID GetNetInterfaceName(PTSTR NetCfgInstanceId, PTSTR IfName)
        {
            HKEY hTestKey;
            CHAR fullKeyName[QC_MAX_VALUE_NAME];

            StringCchCopy((PTSTR)fullKeyName, QC_MAX_VALUE_NAME / 2, TEXT(QC_NET_CONNECTION_REG_KEY));
            StringCchCat((PTSTR)fullKeyName, QC_MAX_VALUE_NAME / 2, NetCfgInstanceId);
            StringCchCat((PTSTR)fullKeyName, QC_MAX_VALUE_NAME / 2, TEXT("\\"));
            StringCchCat((PTSTR)fullKeyName, QC_MAX_VALUE_NAME / 2, TEXT("Connection"));

            if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, (PTSTR)fullKeyName, 0, KEY_READ, &hTestKey) == ERROR_SUCCESS)
            {
                DWORD retCode;
                DWORD valueNameLen = QC_MAX_VALUE_NAME / 2;

                retCode = RegQueryValueEx
                (
                    hTestKey,
                    TEXT("Name"),
                    NULL,         // reserved
                    NULL,         // returned type
                    (LPBYTE)IfName,
                    &valueNameLen
                );

                if (retCode == ERROR_SUCCESS)
                {
                    // QCD_Printf("GetNetInterfaceNames: InterfaceName: [%ws]\n", IfName);
                }
                else
                {
                    QCD_Printf("GetNetInterfaceNames: error 0x%x\n", GetLastError());
                }

                RegCloseKey(hTestKey);
            }
            else
            {
                QCD_Printf("GetNetInterfaceName: faild to open registry 0x%x\n", GetLastError());
            }
        }  // GetNetInterfaceName

        void MatchCaseSerNum(PTSTR InstanceId, PTSTR pSerNumPos, PVOID SerNum)
        {
            //Store serial number in instance id as serial number
            StringCchCopy((PTSTR)SerNum, 128, pSerNumPos);

            HKEY  hSwKey;
            WCHAR keyPath[MAX_PATH];
            CHAR matchingDeviceId[QC_MAX_VALUE_NAME];

            StringCchCopy(keyPath, MAX_PATH, TEXT(QC_REG_HW_KEY_PREF));
            ZeroMemory(matchingDeviceId, QC_MAX_VALUE_NAME);
            StringCchCopyN((PTSTR)matchingDeviceId, QC_MAX_VALUE_NAME / 2, InstanceId, lstrlen(InstanceId) - lstrlen(pSerNumPos));
            StringCchCat(keyPath, MAX_PATH, (PTSTR)matchingDeviceId);

            DWORD retCode = RegOpenKeyEx
            (
                HKEY_LOCAL_MACHINE,
                keyPath,
                0,
                KEY_READ,
                &hSwKey
            );
            if (retCode == ERROR_SUCCESS)
            {
                WCHAR keyName[256];//Max key name is 256 WORD
                DWORD keyLength = 512;//Max key name is 512 CHAR
                WCHAR matchingSerNum[128];//Serial Number is 128 WORD
                WCHAR orgSerNumUpper[128];//Serial Number is 128 WORD
                StringCchCopy(orgSerNumUpper, 128, pSerNumPos);
                _wcsupr_s(orgSerNumUpper, 128);
                DWORD index = 0;
                //Go through the enum key
                while (RegEnumKeyEx(hSwKey, index, keyName, &keyLength, NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
                {
                    //Compare and see if the subkey match the instand id in Capital
                    StringCchCopy(matchingSerNum, 128, keyName);
                    _wcsupr_s(matchingSerNum, 128);
                    if (0 == StrCmp(matchingSerNum, orgSerNumUpper))
                    {
                        //Store key name as serial number
                        StringCchCopy((PTSTR)SerNum, 128, keyName);
                        break;
                    }
                    index++;
                    keyLength = 512;
                    ZeroMemory(keyName, keyLength);
                }
                RegCloseKey(hSwKey);
            }
        }

        PBYTE ValidateDevice
        (
            PTSTR DriverKey,
            PVOID InstanceId,
            PVOID InterfaceName,  // optional, interface name of a NIC
            PVOID SerNum,
            PVOID SerNumMsm,
            PVOID ParentDev,
            PULONG Protocol,
            PLONG Mtu,
            int DevType,
            BOOL *IsActive,
            BOOL *IsQCDriver,
            ULONG DevStatus
        )
        {
            WCHAR keyPath[REG_HW_ID_SIZE];
            static BYTE  regValue[QC_MAX_VALUE_NAME];
            DWORD regLen = QC_MAX_VALUE_NAME;
            HKEY  hSwKey;
            BOOL bResult = FALSE;
            DWORD retCode;

            StringCchCopy(keyPath, MAX_PATH, TEXT(QC_REG_SW_KEY));
            StringCchCat(keyPath, MAX_PATH, DriverKey);

            // QCD_Printf("ValidateDevice: DriverKey <%ws>\n", keyPath);

            // Open device software key
            retCode = RegOpenKeyEx
            (
                HKEY_LOCAL_MACHINE,
                keyPath,
                0,
                KEY_READ,
                &hSwKey
            );

            if (retCode == ERROR_SUCCESS)
            {
                retCode = RegQueryValueEx
                (
                    hSwKey,
                    TEXT(QC_SPEC_STAMP),
                    NULL,         // reserved
                    NULL,         // returned type
                    (LPBYTE)regValue,
                    &regLen
                );
                if (retCode == ERROR_SUCCESS)
                {
                    DWORD v = *((DWORD *)regValue);
                    // QCD_Printf("   QC_SPEC_STAMP: %u for <%ws>\n", v, keyPath);

                    if (v == 1)
                    {
                        *IsActive = TRUE;
                        FeatureSetting.TimerInterval = 1000; // INFINITE; // ADB device needs timer
                    }
                    else
                    {
                        *IsActive = FALSE;
                    }

                    if ((DevStatus & DN_STARTED) != 0)
                    {
                        *IsActive = TRUE;  // Best Effort -- let system devnode status make final call but may not be reliable
                    }
                    *IsQCDriver = TRUE;
                }
                else
                {
                    // not a QC driver
                    *IsActive = TRUE;
                    *IsQCDriver = FALSE;
                }

                regLen = 256;
                retCode = RegQueryValueEx
                (
                    hSwKey,
                    TEXT(QC_SPEC_SERNUM),
                    NULL,         // reserved
                    NULL,         // returned type
                    (LPBYTE)SerNum,
                    &regLen
                );
                if (retCode != ERROR_SUCCESS)
                {
                    // or we use InstanceId instead for USB single function device (for Fastboot)
                    if ((InstanceId != NULL) &&
                        (StrStrW((PTSTR)InstanceId, TEXT(BUS_TEST_USB)) != NULL) &&  // is a USB device
                        (StrStrW((PTSTR)InstanceId, TEXT("&MI_")) == NULL))          // is not a composite device
                    {
                        PTSTR p = (PTSTR)InstanceId, p1;

                        while (p1 = StrStrW(p, TEXT("\\")))
                        {
                            p = ++p1;
                        }

                        if (StrStrW(p, TEXT("&")) != NULL)
                        {
                            ZeroMemory(SerNum, 256);
                            QCD_Printf("ValidateDevice: no SerNum retrieved from <%ws>\n", p);
                        }
                        else
                        {
                            MatchCaseSerNum((PTSTR)InstanceId, p, SerNum);
                            QCD_Printf("ValidateDevice: use <%ws> as SerNum\n", (PTSTR)SerNum);
                        }
                    }
                    else
                    {
                        ZeroMemory(SerNum, 256);
                    }
                }

                regLen = 256;
                retCode = RegQueryValueEx
                (
                    hSwKey,
                    TEXT(QC_SPEC_SERNUM_MSM),
                    NULL,         // reserved
                    NULL,         // returned type
                    (LPBYTE)SerNumMsm,
                    &regLen
                );
                if (retCode != ERROR_SUCCESS)
                {
                    ZeroMemory(SerNumMsm, 256);
                }

                // retrieve protocol code
                regLen = sizeof(ULONG);
                retCode = RegQueryValueEx
                (
                    hSwKey,
                    TEXT(QC_SPEC_PROTOC),
                    NULL,         // reserved
                    NULL,         // returned type
                    (LPBYTE)Protocol,
                    &regLen
                );
                if (retCode != ERROR_SUCCESS)
                {
                    *Protocol = 0;
                }

                // retrieve parent device name
                regLen = 256;
                retCode = RegQueryValueEx
                (
                    hSwKey,
                    TEXT(QC_SPEC_PARENT_DEV),
                    NULL,         // reserved
                    NULL,         // returned type
                    (LPBYTE)ParentDev,
                    &regLen
                );
                if (retCode != ERROR_SUCCESS)
                {
                    ZeroMemory(ParentDev, 256);
                }

                // Retrieve the symbolic link name
                keyPath[0] = 0;
                regLen = QC_MAX_VALUE_NAME;
                if ((DevType == QC_DEV_TYPE_PORTS) || (DevType == QC_DEV_TYPE_MDM))
                {
                    if (*IsQCDriver == TRUE)
                    {
                        retCode = RegQueryValueEx
                        (
                            hSwKey,
                            TEXT(QC_SPEC),
                            NULL,         // reserved
                            NULL,         // returned type
                            (LPBYTE)regValue,
                            &regLen
                        );
                    }
                    else if ((InstanceId != NULL) && ((FeatureSetting.User.Settings & DEV_FEATURE_INCLUDE_NONE_QC_PORTS) != 0))
                    {
                        LPBYTE p;

                        p = GetPortNameFromHwKey((PTSTR)InstanceId);
                        if (p != NULL)
                        {
                            StringCchCopy((PTSTR)regValue, QC_MAX_VALUE_NAME / 2, (PTSTR)p);
                            retCode = ERROR_SUCCESS;
                        }
                        else
                        {
                            retCode = ERROR_FILE_NOT_FOUND;
                        }
                    }
                    else
                    {
                        retCode = ERROR_FILE_NOT_FOUND;
                    }
                }
                else if (DevType == QC_DEV_TYPE_NET)
                {
                    retCode = RegQueryValueEx
                    (
                        hSwKey,
                        TEXT("NetCfgInstanceId"),
                        NULL,         // reserved
                        NULL,         // returned type
                        (LPBYTE)regValue,
                        &regLen
                    );
                    if (InterfaceName != NULL)
                    {
                        GetNetInterfaceName((PTSTR)regValue, (PTSTR)InterfaceName);
                    }

                    regLen = sizeof(LONG);
                    retCode = RegQueryValueEx
                    (
                        hSwKey,
                        TEXT(QC_SPEC_MTU),
                        NULL,         // reserved
                        NULL,         // returned type
                        (LPBYTE)Mtu,
                        &regLen
                    );
                    if (retCode != ERROR_SUCCESS)
                    {
                        *Mtu = 0;
                    }
                    regLen = QC_MAX_VALUE_NAME;
                    retCode = RegQueryValueEx
                    (
                        hSwKey,
                        TEXT(QC_SPEC_NET),
                        NULL,         // reserved
                        NULL,         // returned type
                        (LPBYTE)regValue,
                        &regLen
                    );
                    if (retCode == ERROR_SUCCESS)
                    {
                        *IsActive = TRUE;
                    }
                    else
                    {
                        if ((InstanceId != NULL) &&
                            (StrStrW((PTSTR)InstanceId, TEXT("VID_05C6")) != NULL) &&    // is a QC USB device
                            (StrStrW((PTSTR)InstanceId, TEXT("&MI_")) != NULL))          // is a composite device
                        {
                            *IsActive = TRUE;
                        }
                        else
                        {
                            *IsActive = FALSE;
                            // QCD_Printf("ValidateDevice: RemoveDevice <%ws>\n", InstanceId);
                        }
                    }

                    // Inspect modem SSR
                    if (*IsQCDriver == TRUE)
                    {
                        DWORD ssrRetCode;
                        BYTE  ssrValue[QC_MAX_VALUE_NAME];

                        DWORD ssrLen = QC_MAX_VALUE_NAME;
                        ssrRetCode = RegQueryValueEx
                        (
                            hSwKey,
                            TEXT(QC_SPEC_SSR),
                            NULL,         // reserved
                            NULL,         // returned type
                            (LPBYTE)ssrValue,
                            &ssrLen
                        );
                        if (ssrRetCode == ERROR_SUCCESS)
                        {
                            DWORD v = *((DWORD *)ssrValue);

                            if (v == 1)
                            {
                                *IsActive = FALSE;
                                QCD_Printf("ValidateDevice: SSR detected for <%ws>\n", InstanceId);
                            }
                            else
                            {
                                // *isActive should remain what has been set so far
                                QCD_Printf("ValidateDevice: SSR changed for <%ws> IsActive %d/%d\n", InstanceId, *IsActive, v);
                            }
                        }
                        else
                        {
                            // *isActive should remain what has been set so far
                            // QCD_Printf("ValidateDevice: SSR does not exist for <%ws> IsActive %d\n", InstanceId, *IsActive);
                        }
                    }
                }
                else if (DevType == QC_DEV_TYPE_USB)
                {
                    // for QDSS/DPL online, use FriendlyName, so return NULL
                    retCode = ERROR_FILE_NOT_FOUND; // just assign an error code
                }
                else
                {
                    retCode = ERROR_FILE_NOT_FOUND; // just assign an error code
                }

                if (retCode == ERROR_SUCCESS)
                {
                    bResult = TRUE;
                }
            }
            RegCloseKey(hSwKey);

            if (bResult == TRUE)
            {
                PBYTE pDevNameString = (PBYTE)regValue;

                if (DevType == QC_DEV_TYPE_NET)
                {
                    PCHAR pStart, pEnd;

                    pStart = (PCHAR)regValue;
                    pEnd = pStart + regLen;
                    while (pEnd > pStart)
                    {
                        if (*pEnd != 0x5C)  // look for '\'
                        {
                            pEnd--;
                        }
                        else
                        {
                            pEnd += 2;
                            pDevNameString = (PBYTE)pEnd;
                            break;
                        }
                    }
                }
                return pDevNameString;
            }

            return NULL;
        }  // ValidateDevice

        BOOL IsHexNumber(PVOID NumBuffer)
        {
            PCHAR p = (PCHAR)NumBuffer;
            BOOL retVal = TRUE;

            while ((*p != 0) && (*(p + 1) == 0))
            {
                if ((*p >= '0' && *p <= '9') ||
                    (*p >= 'A' && *p <= 'F') ||
                    (*p >= 'a' && *p <= 'f'))
                {
                    // QCD_Printf("examine: %02X/%c\n", *p, *p);
                }
                else
                {
                    retVal = FALSE;
                    break;
                }
                p += 2;
            }
            return (p == NumBuffer ? FALSE : retVal);
        }  // IsHexNumber

        BOOL FindParent(PVOID HwInstanceId, PVOID ParentDev, PVOID PotentialSerNum)
        {
            WCHAR keyPath[REG_HW_ID_SIZE];
            CHAR  container[256], parentContainer[256];
            DWORD containerLen = 256;
            HKEY  hHwKey;
            DWORD retVal;
            BOOL  result = FALSE;

            StringCchCopy(keyPath, MAX_PATH, TEXT(QC_REG_HW_KEY_PREF));
            StringCchCat(keyPath, MAX_PATH, (PTSTR)HwInstanceId);

            // QCD_Printf("FindParent: path <%ws>\n", keyPath);

            // Open instance and retrieve ContainerID
            if (RegOpenKeyExW
            (
                HKEY_LOCAL_MACHINE,
                keyPath,
                0,
                KEY_READ,
                &hHwKey
                ) == ERROR_SUCCESS
                )
            {
                ZeroMemory(container, containerLen);
                retVal = RegQueryValueExW
                (
                    hHwKey,
                    TEXT("ContainerID"),
                    NULL,
                    NULL,
                    (LPBYTE)container,
                    &containerLen
                );
                RegCloseKey(hHwKey);

                if (retVal == ERROR_SUCCESS)
                {
                    PWSTR pEnd;
                    LSTATUS openSt;

                    // QCD_Printf("FindParent: Container <%ws>\n", (PTSTR)container);

                    // revise keyPath
                    if ((pEnd = StrStrW((PTSTR)keyPath, TEXT("Enum\\USB\\VID_"))) != NULL)
                    {
                        pEnd += 26;
                        *pEnd = 0;
                        // QCD_Printf("FindParent: Rev-path <%ws>\n", keyPath);
                        openSt = RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyPath, KEY_ENUMERATE_SUB_KEYS, KEY_READ, &hHwKey);
                        if (openSt == ERROR_SUCCESS)
                        {
                            DWORD idx = 0;
                            CHAR  subKey[512];
                            DWORD subKeyLen = 512;

                            while (TRUE)
                            {
                                ZeroMemory(subKey, containerLen);
                                subKeyLen = 512;
                                retVal = RegEnumKeyEx
                                (
                                    hHwKey, idx,
                                    (LPWSTR)subKey, &subKeyLen,
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL
                                );
                                if (retVal == ERROR_SUCCESS)
                                {
                                    HKEY hSubKey;

                                    // open the subkey
                                    // QCD_Printf("FindParent: iteration %d <%ws> ret %d\n", idx, (LPWSTR)subKey, retVal);
                                    openSt = RegOpenKeyExW(hHwKey, (LPWSTR)subKey, 0, KEY_READ, &hSubKey);
                                    if (openSt == ERROR_SUCCESS)
                                    {
                                        containerLen = 256;
                                        ZeroMemory(parentContainer, containerLen);
                                        retVal = RegQueryValueExW
                                        (
                                            hSubKey, TEXT("ContainerID"), NULL, NULL,
                                            (LPBYTE)parentContainer, &containerLen
                                        );
                                        if (retVal == ERROR_SUCCESS)
                                        {
                                            // QCD_Printf("FindParent: ParentContainer <%ws>\n", parentContainer);
                                            if (StrCmpW((PCWSTR)parentContainer, (PCWSTR)container) == 0)
                                            {
                                                DWORD devLen = 256;

                                                retVal = RegQueryValueExW
                                                (
                                                    hSubKey, TEXT("FriendlyName"), NULL, NULL,
                                                    (LPBYTE)ParentDev, &devLen
                                                );
                                                if (retVal == ERROR_SUCCESS)
                                                {
                                                    QCD_Printf("FindParent: Parent found <%ws> SubKey <%ws>\n", (PTSTR)ParentDev, subKey);
                                                    if (IsHexNumber((PVOID)subKey) == TRUE)
                                                    {
                                                        StringCchCopy((PTSTR)PotentialSerNum, 128, (PTSTR)subKey);
                                                    }
                                                    result = TRUE;
                                                }
                                                else
                                                {
                                                    QCD_Printf("FindParent: FriendlyName failure 0x%x\n", GetLastError());
                                                }
                                            }
                                        }
                                        RegCloseKey(hSubKey);
                                    }
                                }
                                else
                                {
                                    // until retVal == ERROR_NO_MORE_ITEMS (259)
                                    QCD_Printf("FindParent: Rev-path <%ws> iteration %d error 0x%x Ret %d\n", keyPath, idx, GetLastError(), retVal);
                                    break;  // out of the loop
                                }
                                idx++;
                            } // while

                            RegCloseKey(hHwKey);
                        }
                    }
                }
            }

            return result;
        }  // FindParent

        VOID ScanDevices(VOID)
        {
            HDEVINFO        devInfoHandle = INVALID_HANDLE_VALUE;
            SP_DEVINFO_DATA devInfoData;
            DWORD           memberIdx = 0;
            CHAR            devClass[REG_HW_ID_SIZE];
            CHAR            driverKey[REG_HW_ID_SIZE];
            CHAR            hwId[QC_MAX_VALUE_NAME];
            CHAR            comptId[QC_MAX_VALUE_NAME];
            CHAR            instanceId[512];
            CHAR            potentialSerNum[256];
            CHAR            serNum[256];
            CHAR            serNumMsm[256];
            CHAR            parentDev[256];
            CHAR            ifName[QC_MAX_VALUE_NAME];
            CHAR            friendlyName[QC_MAX_VALUE_NAME];
            CHAR            location[QC_MAX_VALUE_NAME];
            CHAR            devPath[QC_MAX_VALUE_NAME];
            LONG            mtu;
            ULONG           funcProtocol;
            DWORD           requiredSize;
            BOOL            bResult, bHwIdOK;
            BOOL            bMatch, bExclude;
            UCHAR           devType = QC_DEV_TYPE_NONE, busType = QC_DEV_BUS_TYPE_NONE;
            PBYTE           pDevName = NULL;
            BOOL            bActive = FALSE, bQCDriver = FALSE, bInstanceIdOK = FALSE;
            PQC_DEV_ITEM    pItem = NULL;
            PTSTR           pSwIdx = NULL;
            BOOL            bAdbDetected;
            CONFIGRET       cmRet;
            ULONG           devStatus, problemNum;


            devInfoHandle = SetupDiGetClassDevsEx
            (
                NULL,
                NULL,  // TEXT("USB")
                NULL,
                (DIGCF_PRESENT | DIGCF_ALLCLASSES),
                NULL,
                NULL,  // Machine,
                NULL
            );
            if (devInfoHandle == INVALID_HANDLE_VALUE)
            {
                QCD_Printf("SetupDiGetClassDevsEx returned no handle\n");
                return;
            }

            devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
            while (SetupDiEnumDeviceInfo(devInfoHandle, memberIdx, &devInfoData) == TRUE)
            {
                busType = QC_DEV_BUS_TYPE_NONE;
                pDevName = NULL;
                bInstanceIdOK = FALSE;
                bHwIdOK = FALSE;
                bMatch = bExclude = FALSE;
                ZeroMemory(devClass, REG_HW_ID_SIZE);
                ZeroMemory(driverKey, REG_HW_ID_SIZE);
                ZeroMemory(hwId, QC_MAX_VALUE_NAME);
                ZeroMemory(instanceId, 512);
                ZeroMemory(ifName, QC_MAX_VALUE_NAME);
                ZeroMemory(friendlyName, QC_MAX_VALUE_NAME);
                ZeroMemory(location, QC_MAX_VALUE_NAME);
                ZeroMemory(devPath, QC_MAX_VALUE_NAME);
                ZeroMemory(potentialSerNum, 256);
                ZeroMemory(serNum, 256);
                ZeroMemory(serNumMsm, 256);
                ZeroMemory(parentDev, 256);
                bAdbDetected = FALSE;
                devType = QC_DEV_TYPE_NONE;
                pSwIdx = NULL;
                devStatus = problemNum = 0;

                bResult = SetupDiGetDeviceRegistryProperty
                (
                    devInfoHandle,
                    &devInfoData,
                    SPDRP_CLASS,
                    NULL,
                    (LPBYTE)devClass,
                    REG_HW_ID_SIZE,
                    &requiredSize
                );
                if (bResult == FALSE)
                {
                    // QCD_Printf("No class info returned\n");
                    memberIdx++;
                    continue;
                }
                else
                {
                    CharUpper((PTSTR)devClass);
                    if (((FeatureSetting.User.DeviceClass & DEV_CLASS_NET) != 0) ||
                        ((FeatureSetting.User.DeviceClass & DEV_CLASS_MDM) != 0) ||
                        ((FeatureSetting.User.DeviceClass & DEV_CLASS_PORTS) != 0) ||
                        ((FeatureSetting.User.DeviceClass & DEV_CLASS_USB) != 0) ||
                        ((FeatureSetting.User.DeviceClass & DEV_CLASS_ADB) != 0))
                    {
                        // QCD_Printf("Inspecting DevClass <%ws>\n", (PTSTR)devClass);
                        if (StrStrW((PTSTR)devClass, TEXT(D_CLASS_NET)) != NULL)
                        {
                            // QCD_Printf("NET class <%ws>\n", (PTSTR)devClass);
                            devType = QC_DEV_TYPE_NET;
                            bMatch = TRUE;
                        }
                        else if (StrStrW((PTSTR)devClass, TEXT(D_CLASS_PORTS)) != NULL)
                        {
                            // QCD_Printf("PORTS class <%ws>\n", (PTSTR)devClass);
                            devType = QC_DEV_TYPE_PORTS;
                            bMatch = TRUE;
                        }
                        else if (StrStrW((PTSTR)devClass, TEXT(D_CLASS_MODEM)) != NULL)
                        {
                            // QCD_Printf("MDM class <%ws>\n", (PTSTR)devClass);
                            devType = QC_DEV_TYPE_MDM;
                            bMatch = TRUE;
                        }
                        else if (StrStrW((PTSTR)devClass, TEXT(D_CLASS_ADB)) != NULL)
                        {
                            QCD_Printf("ADB class <%ws>\n", (PTSTR)devClass);
                            devType = QC_DEV_TYPE_ADB;
                            bAdbDetected = TRUE;
                            bMatch = TRUE;
                        }
                        else if (StrStrW((PTSTR)devClass, TEXT(D_CLASS_USBDEV)) != NULL)
                        {
                            QCD_Printf("USBDEV class <%ws>\n", (PTSTR)devClass);
                            devType = QC_DEV_TYPE_USB;
                            bMatch = TRUE;
                        }
                    }

                    bResult = SetupDiGetDeviceRegistryProperty
                    (
                        devInfoHandle,
                        &devInfoData,
                        SPDRP_HARDWAREID,
                        NULL,
                        (LPBYTE)hwId,
                        QC_MAX_VALUE_NAME,
                        &requiredSize
                    );
                    if (bResult == TRUE)
                    {
                        bHwIdOK = TRUE;
                    }

                    bResult = SetupDiGetDeviceRegistryProperty
                    (
                        devInfoHandle,
                        &devInfoData,
                        SPDRP_COMPATIBLEIDS,
                        NULL,
                        (LPBYTE)comptId,
                        QC_MAX_VALUE_NAME,
                        &requiredSize
                    );

                    if (bResult == TRUE)
                    {
                        // MBIM
                        if (StrStrW((PTSTR)comptId, TEXT("USB\\Class_02&SubClass_0e")) != NULL)
                        {
                            devType = QC_DEV_TYPE_MBIM;
                        }
                        // RNDIS
                        else if (StrStrW((PTSTR)comptId, TEXT("USB\\MS_COMP_RNDIS")) != NULL)
                        {
                            devType = QC_DEV_TYPE_RNDIS;
                        }
                        else if (StrStrW((PTSTR)comptId, TEXT("USB\\Class_EF&SubClass_04&Prot_01")) != NULL)
                        {
                            devType = QC_DEV_TYPE_RNDIS;
                        }
                    }

                    // filter by USB VID
                    if (bMatch == TRUE)
                    {
                        if ((FeatureSetting.User.Settings & DEV_FEATURE_SCAN_USB_WITH_VID) != 0)
                        {
                            if (StrStrW((PTSTR)hwId, (PTSTR)TEXT(BUS_TEST_USB)) != NULL)
                            {
                                if (StrStrW((PTSTR)hwId, (PTSTR)FeatureSetting.VID) != NULL)
                                {
                                    // match VID
                                    QCD_Printf("USB VID matched <%ws>\n", (PTSTR)hwId);
                                }
                                else
                                {
                                    bMatch = FALSE;
                                }
                            }
                        }
                        // QCD_Printf("Matched class <%ws>... devType %d\n", (PTSTR)devClass, devType);
                    }
                }

                if (bMatch == FALSE)
                {
                    memberIdx++;
                    continue;
                }
                else
                {
                    cmRet = CM_Get_DevNode_Status(&devStatus, &problemNum, devInfoData.DevInst, 0);
                    if (cmRet == CR_SUCCESS)
                    {
                        // if (StrStrW((PTSTR)hwId, TEXT("VID_05C6")) != NULL)
                        // {
                        //    QCD_Printf("\tCM_Get_DevNode_Status 0x%x HWID <%ws>\n", devStatus, (PTSTR)hwId);
                        // }
                        if ((devStatus & DN_STARTED) == 0)
                        {
                            QCD_Printf("\tDevice NOT started.\n");
                            memberIdx++;
                            continue;
                        }
                        else
                        {
                            // QCD_Printf("\tDevice started.\n");

                            // if (StrStrW((PTSTR)hwId, TEXT("VID_05C6")) != NULL)  // limit to QC device for debugging
                            if (StrStrW((PTSTR)hwId, TEXT("USB")) != NULL)  // only re-test USB device
                            {
                                // test again after 100ms
                                Sleep(100);
                                devStatus = 0;
                                cmRet = CM_Get_DevNode_Status(&devStatus, &problemNum, devInfoData.DevInst, 0);
                                // QCD_Printf("\t2ND: CM_Get_DevNode_Status 0x%x HWID <%ws>\n", devStatus, (PTSTR)hwId);
                                if ((devStatus & DN_STARTED) == 0)
                                {
                                    QCD_Printf("\t2ND: Device NOT started.\n");
                                }
                            }  // if
                        }
                    }
                    else
                    {
                        QCD_Printf("\tCM_Get_DevNode_Status failed 0x%x HWID <%ws>\n", cmRet, (PTSTR)hwId);
                        memberIdx++;
                        continue;
                    }
                }

                // retrieve driver/software key
                bResult = SetupDiGetDeviceRegistryProperty
                (
                    devInfoHandle,
                    &devInfoData,
                    SPDRP_DRIVER,
                    NULL,
                    (LPBYTE)driverKey,
                    REG_HW_ID_SIZE,
                    &requiredSize
                );
                if (bResult == FALSE)
                {
                    // QCD_Printf("No driver info returned\n");
                    memberIdx++;
                    continue;
                }
                else
                {
                    pSwIdx = StrStrW((PTSTR)driverKey, TEXT("\\"));
                    if (pSwIdx != NULL)
                    {
                        pSwIdx++;
                        // QCD_Printf("driver key <%ws> IDX <%ws>\n", (PWSTR)driverKey, pSwIdx);
                    }
                }

                // retrieve deivce name for display
                bResult = SetupDiGetDeviceRegistryProperty
                (
                    devInfoHandle,
                    &devInfoData,
                    SPDRP_FRIENDLYNAME,
                    NULL,
                    (LPBYTE)friendlyName,
                    QC_MAX_VALUE_NAME,
                    &requiredSize
                );
                if (bResult == FALSE)
                {
                    SetupDiGetDeviceRegistryProperty
                    (
                        devInfoHandle,
                        &devInfoData,
                        SPDRP_DEVICEDESC,
                        NULL,
                        (LPBYTE)friendlyName,
                        QC_MAX_VALUE_NAME,
                        &requiredSize
                    );

                    // QCD_Printf("<%ws> class <%ws> devType %d ADB %d\n", friendlyName, (PTSTR)devClass, devType, bAdbDetected);

                    if (bAdbDetected == TRUE)
                    {
                        CHAR  custName[QC_MAX_VALUE_NAME];
                        // TODO: for Android, set friendly name
                        StringCchCopy((PTSTR)custName, QC_MAX_VALUE_NAME / 2, (PTSTR)friendlyName);
                        StringCchCat((PTSTR)custName, QC_MAX_VALUE_NAME / 2, TEXT(" ("));
                        StringCchCat((PTSTR)custName, QC_MAX_VALUE_NAME / 2, (PTSTR)pSwIdx);
                        StringCchCat((PTSTR)custName, QC_MAX_VALUE_NAME / 2, TEXT(")"));

                        bResult = SetupDiSetDeviceRegistryProperty
                        (
                            devInfoHandle,
                            &devInfoData,
                            SPDRP_FRIENDLYNAME,
                            (LPBYTE)custName,
                            QC_MAX_VALUE_NAME
                        );
                        if (bResult == TRUE)
                        {
                            QCD_Printf("ADB custName <%ws>\n", (PTSTR)custName);
                            StringCchCopy((PTSTR)friendlyName, QC_MAX_VALUE_NAME / 2, (PTSTR)custName);
                        }
                    }
                }

#ifdef QC_LPC_SUPPORT
                // LPC_TEST -- add filters
                if ((StrStrW((PTSTR)friendlyName, TEXT("LPC Device")) == NULL) &&
                    (StrStrW((PTSTR)friendlyName, TEXT("HS-USB Diagnostics")) == NULL))
                {
                    // QCD_Printf("Skipping <%ws>\n", friendlyName);
                    memberIdx++;
                    continue;
                }
#endif

                if ((StrStrW((PTSTR)friendlyName, TEXT("Qualcomm")) != NULL) ||
                    (StrStrW((PTSTR)friendlyName, TEXT("QDSS")) != NULL))
                {
                    QCD_Printf("<%ws> class <%ws> devType %d ADB %d\n", friendlyName, (PTSTR)devClass, devType, bAdbDetected);
                    if (cmRet == CR_SUCCESS)
                    {
                        if ((devStatus & DN_STARTED) == 0)
                        {
                            QCD_Printf("\tDevice NOT started.\n");
                        }
                        else
                        {
                            QCD_Printf("\tDevice started.\n");
                        }
                    }
                    else
                    {
                        QCD_Printf("\tQDSSDPL: CM_Get_DevNode_Status failed 0x%x\n", cmRet);
                    }
                }

                bResult = SetupDiGetDeviceRegistryProperty
                (
                    devInfoHandle,
                    &devInfoData,
                    SPDRP_LOCATION_INFORMATION,
                    NULL,
                    (LPBYTE)location,
                    QC_MAX_VALUE_NAME,
                    &requiredSize
                );

                if (bResult == FALSE)
                {
                    if (StrStrW((PTSTR)hwId, (PTSTR)TEXT(BUS_TEST_USB)) != NULL)
                    {
                        QCD_Printf("\tLOCATION_INFORMATION failure: 0x%x\n", GetLastError());
                    }
                }

                bResult = SetupDiGetDeviceRegistryProperty
                (
                    devInfoHandle,
                    &devInfoData,
                    SPDRP_LOCATION_PATHS,
                    NULL,
                    (LPBYTE)devPath,
                    QC_MAX_VALUE_NAME,
                    &requiredSize
                );
                if (bResult == FALSE)
                {
                    if (StrStrW((PTSTR)hwId, (PTSTR)TEXT(BUS_TEST_USB)) != NULL)
                    {
                        QCD_Printf("\tLOCATION_PATHS failure: 0x%x\n", GetLastError());
                    }
                }

                // if location is not available, use devPath
                if ((location[0] == 0) && (location[1] == 0))
                {
                    CopyMemory((PVOID)location, (PVOID)devPath, QC_MAX_VALUE_NAME);
                }

                // Get a dev item
                EnterCriticalSection(&opLock);
                if (!IsListEmpty(&FreeList))
                {
                    PLIST_ENTRY pEntry;

                    pEntry = RemoveHeadList(&FreeList);
                    pItem = CONTAINING_RECORD(pEntry, QC_DEV_ITEM, List);
                    LeaveCriticalSection(&opLock);
                    pItem->Info.Type = devType;
                }
                else
                {
                    QCD_Printf("CRITICAL: no pItem for dev\n");
                    LeaveCriticalSection(&opLock);
                    pItem = NULL;
                }

                requiredSize = 0;
                bInstanceIdOK = SetupDiGetDeviceInstanceId
                (
                    devInfoHandle,
                    &devInfoData,
                    (PWSTR)instanceId,
                    256,
                    &requiredSize
                );

                if (bInstanceIdOK == TRUE)
                {
                    if (StrStrW((PTSTR)instanceId, TEXT(BUS_TEST_USB)) != NULL)
                    {
                        busType = QC_DEV_BUS_TYPE_USB;
                    }
                    else if (StrStrW((PTSTR)instanceId, TEXT(BUS_TEST_PCI)) != NULL)
                    {
                        busType = QC_DEV_BUS_TYPE_PCI;
                    }
                    else if (StrStrW((PTSTR)instanceId, TEXT(BUS_TEST_PCIE)) != NULL)
                    {
                        busType = QC_DEV_BUS_TYPE_PCIE;
                    }
                    // QCD_Printf("ScanDevices: <%ws> BusType (%d) <%ws>\n", (PWSTR)instanceId, busType, (PTSTR)friendlyName);
                }

                if ((bInstanceIdOK == TRUE) && ((devType == QC_DEV_TYPE_PORTS) || (devType == QC_DEV_TYPE_MDM)))
                {

                    pDevName = ValidateDevice
                    (
                        (PTSTR)driverKey,
                        (PVOID)instanceId,
                        (PVOID)ifName,
                        (PVOID)serNum,
                        (PVOID)serNumMsm,
                        (PVOID)parentDev,
                        &funcProtocol,
                        &mtu,
                        devType,
                        &bActive,
                        &bQCDriver,
                        devStatus
                    );
                }
                else
                {
                    pDevName = ValidateDevice
                    (
                        (PTSTR)driverKey,
                        (PVOID)instanceId, // NULL,
                        (PVOID)ifName,
                        (PVOID)serNum,
                        (PVOID)serNumMsm,
                        (PVOID)parentDev,
                        &funcProtocol,
                        &mtu,
                        devType,
                        &bActive,
                        &bQCDriver,
                        devStatus
                    );
                }

                if (bActive == TRUE)
                {
                    if (pItem != NULL)
                    {
                        size_t rtnBytes;

                        pItem->Info.Type = devType;
                        pItem->Info.Flag = DEV_FLAG_NONE;
                        pItem->Info.IsQCDriver = (UCHAR)bQCDriver;
                        pItem->CbParams.Protocol = funcProtocol;
                        pItem->CbParams.Mtu = (ULONG)mtu;
                        pItem->BusType = busType;
                        StringCchCopy((PTSTR)pItem->DevDesc, QC_MAX_VALUE_NAME / 2, (PTSTR)friendlyName);
                        wcstombs_s(&rtnBytes, pItem->DevDescA, QC_MAX_VALUE_NAME, (PWCHAR)pItem->DevDesc, _TRUNCATE);

                        StringCchCopy((PTSTR)pItem->DevNameW, QC_MAX_VALUE_NAME / 2, TEXT(QC_DEV_PREFIX));
                        if (pDevName != NULL)
                        {
                            StringCchCat((PTSTR)pItem->DevNameW, QC_MAX_VALUE_NAME / 2, (PTSTR)pDevName);
                        }
                        else
                        {
                            // QDSS/DPL - use friendly name as device name
                            StringCchCat((PTSTR)pItem->DevNameW, QC_MAX_VALUE_NAME / 2, (PTSTR)friendlyName);
                        }
                        wcstombs_s(&rtnBytes, pItem->DevNameA, QC_MAX_VALUE_NAME, (PWCHAR)pItem->DevNameW, _TRUNCATE);
                        if (bHwIdOK == TRUE)
                        {
                            StringCchCopy((PTSTR)pItem->HwId, QC_MAX_VALUE_NAME / 2, (PTSTR)hwId);
                        }
                        if (devType == QC_DEV_TYPE_NET)
                        {
                            StringCchCopy((PTSTR)pItem->InterfaceName, QC_MAX_VALUE_NAME / 2, (PTSTR)ifName);
                        }
                        StringCchCopy((PTSTR)pItem->Location, QC_MAX_VALUE_NAME / 2, (PTSTR)location);
                        StringCchCopy((PTSTR)pItem->DevPath, QC_MAX_VALUE_NAME / 2, (PTSTR)devPath);
                        StringCchCopy((PTSTR)pItem->SerNum, 128, (PTSTR)serNum);
                        StringCchCopy((PTSTR)pItem->SerNumMsm, 128, (PTSTR)serNumMsm);
                        // if there's no parent name, use the DevDesc
                        if ((parentDev[0] == 0) && (parentDev[1] == 0))
                        {
                            QCD_Printf(" To find parent for <%ws> <%ws>\n", (PTSTR)pItem->DevNameW, (PTSTR)pItem->DevDesc);
                            if (FALSE == FindParent((PVOID)instanceId, (PVOID)parentDev, (PVOID)potentialSerNum))
                            {
                                StringCchCopy((PTSTR)pItem->ParentDev, 128, (PTSTR)friendlyName);
                            }
                            else
                            {
                                StringCchCopy((PTSTR)pItem->ParentDev, 128, (PTSTR)parentDev);
                                if ((serNum[0] == 0) && (serNum[1] == 0))
                                {
                                    // StringCchCopy((PTSTR)pItem->SerNum, 128, (PTSTR)potentialSerNum);
                                    QCD_Printf("Ignore potential SerNum from parent for <%ws>\n", (PTSTR)pItem->DevDesc);

                                }
                            }
                        }
                        else
                        {
                            StringCchCopy((PTSTR)pItem->ParentDev, 128, (PTSTR)parentDev);
                        }
                        InsertTailList(&NewArrivalList, &pItem->List);
                        // QCD_Printf("Dev Added: <%ws> HWID <%ws> INST <%ws> 0x%p\n", (PTSTR)pItem->DevDesc, (PTSTR)hwId, (PWSTR)instanceId, pItem);
                    }
                }  // if
                else
                {
                    // QCD_Printf("Dev failed to be added: <%ws>\n", (PTSTR)pItem->DevDesc);
                    EnterCriticalSection(&opLock);
                    InsertHeadList(&FreeList, &pItem->List);
                    LeaveCriticalSection(&opLock);
                }

                memberIdx++;
            }  // while

            if (devInfoHandle != INVALID_HANDLE_VALUE)
            {
                SetupDiDestroyDeviceInfoList(devInfoHandle);
            }

            return;

        }  // ScanDevices

        DWORD WINAPI RunDevMonitor(PVOID Context)
        {
            DWORD status = WAIT_OBJECT_0;
            static BOOL bScannedAlready = FALSE;
            DWORD  tid;

            while (bMonitorRunning == TRUE)  // set in StartDeviceMonitor()
            {
                if (bScannedAlready == FALSE)
                {
                    bScannedAlready = TRUE;
                    hAnnouncementThread = ::CreateThread(NULL, 0, AnnouncementThread, NULL, 0, &tid);
                    SetEvent(hMonitorStartedEvt);
                }
                // if (status == WAIT_OBJECT_0)
                if (status < QC_REG_MAX)
                {
                    // QCD_Printf("scanning with alert %u...\n", status);
                    ScanDevices();
                    TryToAnnounce(&ArrivalList, &NewArrivalList);
                }
                status = MonitorDeviceChange();
            }
            SetEvent(hAnnouncementEvt);  // last announcement to clean up queues
            WaitForSingleObject(hAnnouncementExitEvt, 1000);
            CleanupList(&ArrivalList);
            CleanupList(&NewArrivalList);
            CleanupList(&FreeList);
            if (NotifyStore != NULL)
            {
                free(NotifyStore);
            }
            QCD_Printf("exiting device monitor...\n");
            SetEvent(hStopMonitorEvt);  // signal after cleaning up
            bScannedAlready = FALSE;

            return 0;
        }  // RunDevMonitor

    }  // anonymous namespace

    VOID DisplayDevices(VOID)
    {
        PQC_DEV_ITEM pDevInfo;
        PLIST_ENTRY headOfArrival, peekArrival;

        QCD_Printf("================ DEVICES =================\n");

        EnterCriticalSection(&opLock);

        if (!IsListEmpty(&ArrivalList))
        {
            headOfArrival = &ArrivalList;
            peekArrival = headOfArrival->Flink;
            while (peekArrival != headOfArrival)
            {
                pDevInfo = CONTAINING_RECORD(peekArrival, QC_DEV_ITEM, List);
                peekArrival = peekArrival->Flink;
                QCD_Printf("[Type_%d] <%ws>\n", pDevInfo->Info.Type, pDevInfo->DevDesc);
                QCD_Printf("   Dev-to-open <%ws>\n", pDevInfo->DevNameW);
            }
        }

        LeaveCriticalSection(&opLock);
    }  // DisplayDevices

    // ConfigureCommChannel: Initial implementation for quick validation
    // Configure communication channel, error codes to be defined later
    int ConfigureCommChannel(UCHAR DevType, HANDLE DeviceHandle, DWORD Baudrate, BOOL IsLegacyTimeoutConfig)
    {
        int retVal = 0;

        if (DevType == QC_DEV_TYPE_PORTS)
        {
            DCB dcb;
            COMMTIMEOUTS commTimeouts;

            dcb.DCBlength = sizeof(DCB);
            if (GetCommState(DeviceHandle, &dcb) == TRUE)
            {
                dcb.BaudRate = Baudrate;
                dcb.ByteSize = 8;
                dcb.Parity = NOPARITY;
                dcb.StopBits = ONESTOPBIT;
                if (SetCommState(DeviceHandle, &dcb) == FALSE)
                {
                    QCD_Printf("[Type_%d] SetCommState failed on handle 0x%x, error 0x%x\n",
                               DevType, DeviceHandle, GetLastError());
                    retVal = 1;
                }
                else
                {
                    QCD_Printf("[Type_%d] SetCommState done", DevType);
                }
            }
            else
            {
                QCD_Printf("[Type_%d] GetCommState failed on handle 0x%x, error 0x%x\n",
                           DevType, DeviceHandle, GetLastError());
                retVal = 2;
            }

            if (IsLegacyTimeoutConfig)
            {
                commTimeouts.ReadIntervalTimeout = 20;
                commTimeouts.ReadTotalTimeoutMultiplier = 0;
                commTimeouts.ReadTotalTimeoutConstant = 100;
                commTimeouts.WriteTotalTimeoutMultiplier = 1;
                commTimeouts.WriteTotalTimeoutConstant = 10;
            }
            else
            {
                commTimeouts.ReadIntervalTimeout = MAXDWORD;
                commTimeouts.ReadTotalTimeoutMultiplier = MAXDWORD;
                commTimeouts.ReadTotalTimeoutConstant = 100;
                commTimeouts.WriteTotalTimeoutMultiplier = 0;
                commTimeouts.WriteTotalTimeoutConstant = 2000;
            }

            if (SetCommTimeouts(DeviceHandle, &commTimeouts) == false)
            {
                QCD_Printf("[Type_%d] SetCommTimeouts failed on handle 0x%x Error 0x%x\n",
                           DevType, DeviceHandle, GetLastError());
                retVal = 3;
            }
            else
            {
                QCD_Printf("[Type_%d] SetCommTimeouts done", DevType);
            }
        }
        return retVal;
    }  // ConfigureCommChannel
}  // QcDevice

// ----------- PUBLIC APIs -------------

QCDEVLIB_API VOID QcDevice::StartDeviceMonitor(VOID)
{
    DWORD  tid;

    if (InterlockedIncrement(&lInOperation) > 1)
    {
        InterlockedDecrement(&lInOperation);
        return;
    }

    if (bMonitorRunning == FALSE)
    {
        if (bFeatureSet == FALSE)
        {
            FeatureSetting.User.Version = 1;
            FeatureSetting.User.Settings = DEV_FEATURE_INCLUDE_NONE_QC_PORTS;
            FeatureSetting.User.DeviceClass = (DEV_CLASS_NET | DEV_CLASS_PORTS | DEV_CLASS_USB);
            FeatureSetting.TimerInterval = 1000; // INFINITE;  // ADB device needs timer
            ZeroMemory(FeatureSetting.VID, QC_MAX_VALUE_NAME);
        }
        InitializeCriticalSection(&opLock);
        InitializeCriticalSection(&notifyLock);
        hStopMonitorEvt = CreateEvent(NULL, FALSE, FALSE, NULL);
        hMonitorStartedEvt = CreateEvent(NULL, FALSE, FALSE, NULL);
        hAnnouncementEvt = CreateEvent(NULL, FALSE, FALSE, NULL);
        hAnnouncementExitEvt = CreateEvent(NULL, FALSE, FALSE, NULL);
        InitializeLists();
        bMonitorRunning = TRUE;
        hMonitorThread = ::CreateThread(NULL, 0, RunDevMonitor, NULL, 0, &tid);
        if (hMonitorThread == NULL)
        {
            bMonitorRunning = FALSE;
        }
        WaitForSingleObject(hMonitorStartedEvt, 5000);
        CloseHandle(hMonitorStartedEvt);  // dispose
    }

    InterlockedDecrement(&lInOperation);

    return;
}  // StartDeviceMonitor

QCDEVLIB_API VOID QcDevice::StopDeviceMonitor(VOID)
{
    int i;

    bMonitorRunning = FALSE;
    SetEvent(hRegChangeEvt[0]);
    WaitForSingleObject(hStopMonitorEvt, 10000);
    if (hAnnouncementThread != 0)
    {
        CloseHandle(hAnnouncementThread);
        hAnnouncementThread = 0;
    }
    if (hMonitorThread != 0)
    {
        CloseHandle(hMonitorThread);
        hMonitorThread = 0;
    }
    CloseHandle(hAnnouncementEvt);
    CloseHandle(hAnnouncementExitEvt);
    CloseHandle(hStopMonitorEvt);

    for (i = QC_REG_DEVMAP; i < QC_REG_MAX; i++)
    {
        CloseHandle(hRegChangeEvt[i]);
        CloseHandle(hSysKey[i]);
    }
    DeleteCriticalSection(&opLock);
    DeleteCriticalSection(&notifyLock);
    lInitialized = 0;
}  // StopDeviceMonitor

// =================== Removal Notification ====================
QCDEVLIB_API VOID QcDevice::SetDeviceChangeCallback(DEVICECHANGE_CALLBACK Cb)
{
    fNotifyCb = Cb;
    CallerContext = NULL;
}  // SetDeviceChangeCallback

QCDEVLIB_API VOID QcDevice::SetDeviceChangeCallback(DEVICECHANGE_CALLBACK Cb, PVOID AppContext)
{
    fNotifyCb = Cb;
    CallerContext = AppContext;
}  // SetDeviceChangeCallback

QCDEVLIB_API VOID QcDevice::SetDeviceChangeCallback(DEVICECHANGE_CALLBACK_N Cb)
{
    fNotifyCb_N = Cb;
}  // SetDeviceChangeCallback

QCDEVLIB_API VOID QcDevice::SetLoggingCallback(QCD_LOGGING_CALLBACK Cb)
{
    fLogger = Cb;
}  // SetLoggingCallback

QCDEVLIB_API VOID QcDevice::SetFeature(PVOID Settings)
{
    PDEV_FEATURE_SETTING pFeature = (PDEV_FEATURE_SETTING)Settings;

    if (pFeature->Version != 1)
    {
        return;
    }
    FeatureSetting.User.Version = pFeature->Version;
    FeatureSetting.User.Settings = pFeature->Settings;
    FeatureSetting.User.DeviceClass = pFeature->DeviceClass;
    ZeroMemory(FeatureSetting.VID, QC_MAX_VALUE_NAME);

    FeatureSetting.TimerInterval = 1000; // INFINITE; // 500; // ADB device needs timer

    bFeatureSet = TRUE;

    if ((FeatureSetting.User.Settings & DEV_FEATURE_SCAN_USB_WITH_VID) != 0)
    {
        if (pFeature->VID != NULL)
        {
            StringCchCopy((PTSTR)FeatureSetting.VID, QC_MAX_VALUE_NAME / 2, (PTSTR)pFeature->VID);
        }
    }

} // SetFeature

QCDEVLIB_API ULONG QcDevice::GetDevice(PVOID UserBuffer)
{
    PDEV_PARAMS_N DevInfo = (PDEV_PARAMS_N)UserBuffer;
    PQC_NOTIFICATION_STORE storeBranch;
    PQC_DEV_ITEM devItem;
    PLIST_ENTRY headOfList;
    ULONG infoFilled = 0, returnVal = DEV_INFO_OK;

    QCD_Printf("-->GetDevice (%d, %d, %d, %d, %d)\n",
               DevInfo->DevDescBufLen, DevInfo->DevnameBufLen, DevInfo->IfNameBufLen,
               DevInfo->LocBufLen, DevInfo->SerNumBufLen);

    EnterCriticalSection(&notifyLock);
    if (!IsListEmpty(&AnnouncementList))
    {
        headOfList = RemoveHeadList(&AnnouncementList);
        storeBranch = CONTAINING_RECORD(headOfList, QC_NOTIFICATION_STORE, List);
        LeaveCriticalSection(&notifyLock);

        while (!IsListEmpty(&(storeBranch->DevItemChain)))
        {
            headOfList = RemoveHeadList(&(storeBranch->DevItemChain));
            devItem = CONTAINING_RECORD(headOfList, QC_DEV_ITEM, List);

            if (DevInfo->DevDescBufLen > 0)
            {
                ZeroMemory(DevInfo->DevDesc, DevInfo->DevDescBufLen);
                StringCchCopy((PTSTR)DevInfo->DevDesc, DevInfo->DevDescBufLen / 2, (PTSTR)devItem->DevDesc);
            }
            else
            {
                returnVal = DEV_INFO_DEV_DESC_LEN;
                break;
            }
            if (DevInfo->DevnameBufLen > 0)
            {
                ZeroMemory(DevInfo->DevName, DevInfo->DevnameBufLen);
                StringCchCopy((PTSTR)DevInfo->DevName, DevInfo->DevnameBufLen, (PTSTR)devItem->DevNameA);
            }
            else
            {
                returnVal = DEV_INFO_DEV_NAME_LEN;
                break;
            }
            if (DevInfo->IfNameBufLen > 0)
            {
                ZeroMemory(DevInfo->IfName, DevInfo->IfNameBufLen);
                StringCchCopy((PTSTR)DevInfo->IfName, DevInfo->IfNameBufLen / 2, (PTSTR)devItem->InterfaceName);
            }
            else
            {
                returnVal = DEV_INFO_IF_NAME_LEN;
                break;
            }
            if (DevInfo->LocBufLen > 0)
            {
                ZeroMemory(DevInfo->Loc, DevInfo->LocBufLen);
                StringCchCopy((PTSTR)DevInfo->Loc, DevInfo->LocBufLen / 2, (PTSTR)devItem->Location);
            }
            if (DevInfo->DevPathBufLen > 0)
            {
                ZeroMemory(DevInfo->DevPath, DevInfo->DevPathBufLen);
                StringCchCopy((PTSTR)DevInfo->DevPath, DevInfo->DevPathBufLen / 2, (PTSTR)devItem->DevPath);
            }
            else
            {
                returnVal = DEV_INFO_LOC_LEN;
                break;
            }
            if (DevInfo->SerNumBufLen > 0)
            {
                ZeroMemory(DevInfo->SerNum, DevInfo->SerNumBufLen);
                StringCchCopy((PTSTR)DevInfo->SerNum, DevInfo->SerNumBufLen / 2, (PTSTR)devItem->SerNum);
            }
            if (DevInfo->SerNumMsmBufLen > 0)
            {
                ZeroMemory(DevInfo->SerNumMsm, DevInfo->SerNumMsmBufLen);
                StringCchCopy((PTSTR)DevInfo->SerNumMsm, DevInfo->SerNumMsmBufLen / 2, (PTSTR)devItem->SerNumMsm);
            }
            else
            {
                returnVal = DEV_INFO_SER_NUM_LEN;
                break;
            }
            DevInfo->Mtu = 0;  // not used

            if (devItem->Info.Flag == DEV_FLAG_ARRIVAL)
            {
                DevInfo->Flag = (((ULONG)devItem->Info.Type << 8) | ((ULONG)1 << 4) |
                                 (ULONG)devItem->Info.IsQCDriver);
            }
            else
            {
                DevInfo->Flag = (((ULONG)devItem->Info.Type << 8) | (ULONG)devItem->Info.IsQCDriver);
            }

            free(devItem);
            infoFilled = 1;
            break;
        }
        if ((infoFilled == 0) && (returnVal != DEV_INFO_OK))
        {
            QCD_Printf("GetDevice: error %d, restore the device info for future retrieval\n", returnVal);
            InsertHeadList(&(storeBranch->DevItemChain), &(devItem->List)); // re-store
        }

        EnterCriticalSection(&notifyLock);
        if (IsListEmpty(&(storeBranch->DevItemChain)))
        {
            QCD_Printf("GetDevice: recycle storeBranch\n");
            InsertTailList(&NotifyFreeList, &(storeBranch->List)); // recycle
        }
        else
        {
            QCD_Printf("GetDevice: restore storeBranch\n");
            InsertHeadList(&AnnouncementList, &(storeBranch->List)); // restore
        }
    }  // if
    LeaveCriticalSection(&notifyLock);

    if ((returnVal == DEV_INFO_OK) && (infoFilled == 0))
    {
        QCD_Printf("GetDevice: no more device\n");
        returnVal = DEV_INFO_END;
    }

    QCD_Printf("<--GetDevice (ST %d, filled %d Flag 0x%x)\n", returnVal, infoFilled, DevInfo->Flag);
    return returnVal;
}  // GetDevice

QCDEVLIB_API PCHAR QcDevice::GetPortName(PVOID DeviceName)
{
    size_t length;
    PCHAR p;

    p = (PCHAR)DeviceName;
    length = strlen(p);
    p += length;
    while (p > DeviceName)
    {
        if (*p == 0x5C)  // '\'
        {
            break;
        }
        p--;
    }

    if (p == DeviceName)
    {
        return NULL;
    }

    return (p + 1);
}  // GetPortName

QCDEVLIB_API PCHAR QcDevice::LibVersion(VOID)
{
    return "1.0.0.0";
}

QCDEVLIB_API VOID _cdecl QcDevice::QCD_Printf(const char *Format, ...)
{
#define DBG_MSG_MAX_SZ 1024
#define MSG_LEN_MAX (DBG_MSG_MAX_SZ - 50)
    va_list arguments;
    CHAR   msgBuffer[DBG_MSG_MAX_SZ], *pBuf;
    SYSTEMTIME lt;

    // log time
    GetLocalTime(&lt);
    _snprintf_s((PCHAR)msgBuffer, DBG_MSG_MAX_SZ, _TRUNCATE, "%s-[%02d:%02d:%02d.%03d] ",
                LibVersion(), lt.wHour, lt.wMinute, lt.wSecond, lt.wMilliseconds);
    pBuf = (PCHAR)msgBuffer + strlen(msgBuffer);

    // log data
    va_start(arguments, Format);
    _vsnprintf_s((PCHAR)pBuf, MSG_LEN_MAX, _TRUNCATE, Format, arguments);
    va_end(arguments);

    if (fLogger != NULL)
    {
        fLogger((PCHAR)msgBuffer);
    }
    else
    {
#ifdef _DEBUG
#ifdef TOOLS_TARGET_WINDOWS
        OutputDebugStringA((PCHAR)msgBuffer);
#else
        Lang::cout << msgBuffer << std::endl;
#endif
#endif

    }
}  // QCD_Printf

QCDEVLIB_API ULONG QcDevice::GetDeviceList(PVOID Buffer, ULONG BufferSize, PULONG ActualSize)
{
    PQC_DEV_ITEM pDevInfo;
    PLIST_ENTRY headOfArrival, peekArrival;
    PCHAR pDest;
    size_t remainingSpace, nameLen;
    ULONG numOfDevices = 0;

    remainingSpace = BufferSize;
    pDest = (PCHAR)Buffer;
    *ActualSize = 0;

    EnterCriticalSection(&opLock);

    ZeroMemory(Buffer, BufferSize);
    if (!IsListEmpty(&ArrivalList))
    {
        headOfArrival = &ArrivalList;
        peekArrival = headOfArrival->Flink;
        while (peekArrival != headOfArrival)
        {
            pDevInfo = CONTAINING_RECORD(peekArrival, QC_DEV_ITEM, List);
            peekArrival = peekArrival->Flink;

            // copy DevDescA to the supplied buffer
            nameLen = strlen(pDevInfo->DevDescA);
            if (0 != strncpy_s(pDest, remainingSpace, pDevInfo->DevDescA, nameLen))
            {
                QCD_Printf("GetDeviceList error\n");
                break;
            }
            else
            {
                pDest += (nameLen + 1); // including the NULL
                remainingSpace -= (nameLen + 1);
                *ActualSize += (ULONG)(nameLen + 1);
                numOfDevices++;
            }
        }
    }

    LeaveCriticalSection(&opLock);

    return numOfDevices;

}  // GetDeviceList

QCDEVLIB_API HANDLE QcDevice::OpenDevice(PVOID DeviceName, DWORD Baudrate, BOOL IslegacyTimeoutConfig)
{
    PQC_DEV_ITEM pDevInfo;
    PLIST_ENTRY headOfArrival, peekArrival;
    PVOID pDevName = NULL;
    HANDLE hDevice = INVALID_HANDLE_VALUE;  // -1
    CHAR usbDev[256];

    QCD_Printf("-->QcDevice::OpenDevice: <%s>\n", DeviceName);

    if ((StrStrA((PCHAR)DeviceName, "QDSS") == NULL) && (StrStrA((PCHAR)DeviceName, "DPL") == NULL))
    {
        EnterCriticalSection(&opLock);
        if (!IsListEmpty(&ArrivalList))
        {
            headOfArrival = &ArrivalList;
            peekArrival = headOfArrival->Flink;
            while (peekArrival != headOfArrival)
            {
                pDevInfo = CONTAINING_RECORD(peekArrival, QC_DEV_ITEM, List);
                peekArrival = peekArrival->Flink;
                // QCD_Printf("-->QcDevice::OpenDevice:comparing <%s> <%s>\n", (PCHAR)DeviceName, (PCHAR)pDevInfo->DevDescA);
                if (StrCmpA((PCHAR)DeviceName, (PCHAR)pDevInfo->DevDescA) == 0)
                {
                    pDevName = (PVOID)pDevInfo->DevNameA;
                    break;
                }
            }
        }
        LeaveCriticalSection(&opLock);
    }
    else // QDSS or DPL case
    {
        strncpy_s(usbDev, sizeof(usbDev), "\\\\.\\", _TRUNCATE);
        strncat_s(usbDev, sizeof(usbDev), (PCHAR)DeviceName, _TRUNCATE);
        pDevName = usbDev;
    }

    // pDevName = DeviceName;  // short-cut for testing

    if (pDevName != NULL)
    {
        hDevice = ::CreateFileA
        (
            (PCHAR)pDevName,
            GENERIC_WRITE | GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, // async operation
            NULL
        );

        if (hDevice == INVALID_HANDLE_VALUE)
        {
            QCD_Printf("QcDevice::OpenDevice: Error 0x%x\n, attempt read only", GetLastError());
        }
        ConfigureCommChannel(QC_DEV_TYPE_PORTS, hDevice, Baudrate, IslegacyTimeoutConfig);
    }
    else
    {
        QCD_Printf("QcDevice::OpenDevice: cannot find device <%s>\n", (PCHAR)DeviceName);
    }

    QCD_Printf("<--QcDevice::OpenDevice: <%s> 0x%x\n", DeviceName, hDevice);
    return hDevice;
}  // OpenDevice

QCDEVLIB_API VOID QcDevice::CloseDevice(HANDLE hDevice)
{
    QCD_Printf("QcDevice::CloseDevice: 0x%x\n", hDevice);

    ::CloseHandle(hDevice);
}  // CloseDevice

QCDEVLIB_API BOOL QcDevice::ReadFromDevice
(
    HANDLE hDevice,
    PVOID  RxBuffer,
    DWORD  NumBytesToRead,
    LPDWORD NumBytesReturned
)
{
    OVERLAPPED ov;
    BOOL       bResult = FALSE;
    DWORD      dwStatus = NO_ERROR;

    QCD_Printf("-->QcDevice::ReadFromDevice: 0x%x bufferSize %d bytes\n", hDevice, NumBytesToRead);

    ZeroMemory(&ov, sizeof(ov));
    ov.Offset = 0;
    ov.OffsetHigh = 0;
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (ov.hEvent == NULL)
    {
        QCD_Printf("QcDevice::ReadFromDevice event error %u\n", GetLastError());
        return bResult;
    }
    *NumBytesReturned = 0;

    bResult = ::ReadFile
    (
        hDevice,
        RxBuffer,
        NumBytesToRead,
        NumBytesReturned,
        &ov
    );

    if (bResult == TRUE)
    {
        if (ov.hEvent != NULL)
        {
            CloseHandle(ov.hEvent);
        }
        return bResult;
    }
    else
    {
        dwStatus = GetLastError();

        if (ERROR_IO_PENDING != dwStatus)
        {
            QCD_Printf("QcDevice::ReadFromDevice failure, error %u\n", dwStatus);
        }
        else
        {
            bResult = GetOverlappedResult
            (
                hDevice,
                &ov,
                NumBytesReturned,
                TRUE  // no return until operaqtion completes
            );

            if (bResult == FALSE)
            {
                dwStatus = GetLastError();
                QCD_Printf("QcDevice::ReadFromDevice/GetOverlappedResult failure %u\n", dwStatus);
            }
        }
    }

    if (ov.hEvent != NULL)
    {
        CloseHandle(ov.hEvent);
    }
    QCD_Printf("<--QcDevice::ReadFromDevice: 0x%x read %d bytes (result %d)\n", hDevice, *NumBytesReturned, bResult);

    return bResult;
}  // ReadFromDevice

QCDEVLIB_API BOOL QcDevice::SendToDevice
(
    HANDLE hDevice,
    PVOID  TxBuffer,
    DWORD  NumBytesToSend,
    LPDWORD NumBytesSent
)
{
    BOOL       bResult = FALSE;
    OVERLAPPED ov;
    DWORD      dwStatus = NO_ERROR;

    QCD_Printf("-->QcDevice::SendToDevice: 0x%x %d bytes\n", hDevice, NumBytesToSend);

    ZeroMemory(&ov, sizeof(ov));
    ov.Offset = 0;
    ov.OffsetHigh = 0;
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (ov.hEvent == NULL)
    {
        QCD_Printf("Read error, event error %u\n", GetLastError());
        return bResult;
    }

    *NumBytesSent = 0;

    if (NumBytesToSend != 0)
    {
        bResult = ::WriteFile
        (
            hDevice,
            TxBuffer,
            NumBytesToSend,
            NumBytesSent,
            &ov
        );
        if (bResult == FALSE)
        {
            dwStatus = GetLastError();

            if (ERROR_IO_PENDING != dwStatus)
            {
                QCD_Printf("QcDevice::SendToDevice-0 error %u\n", dwStatus);
            }
            else
            {
                bResult = GetOverlappedResult
                (
                    hDevice,
                    &ov,
                    NumBytesSent,
                    TRUE  // no return until operaqtion completes
                );

                if (bResult == FALSE)
                {
                    dwStatus = GetLastError();
                    QCD_Printf("QcDevice::SendToDevice-1 %u\n", dwStatus);
                }
            }
        }
    }
    else
    {
        QCD_Printf("QcDevice::SendToDevice - nothing to send\n");
    }

    if (ov.hEvent != NULL)
    {
        CloseHandle(ov.hEvent);
    }

    QCD_Printf("-->QcDevice::SendToDevice: 0x%x sent %d bytes (result %d)\n", hDevice, *NumBytesSent, bResult);
    return bResult;
}  // SendToDevice

