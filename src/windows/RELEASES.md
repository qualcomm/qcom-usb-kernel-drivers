# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause

# Release Notes

All notable changes to the **Qualcomm USB Kernel Drivers (QUD)** Windows drivers are documented in this file.

## Component Description

Qualcomm USB Host Drivers are built for Windows and are intended for use with Qualcomm USB Host hardware and firmware supporting all Qualcomm VID/PIDs. Supported architectures include x86, x64, and ARM (Windows 8 serial driver only).

- **Open source repository:** https://github.com/qualcomm/qcom-usb-kernel-drivers.git
- **Contributing:** If you are interested in contributing to the open-source Qualcomm USB Kernel Driver, please refer to [CONTRIBUTING.md](../../CONTRIBUTING.md), or raise an issue on GitHub for any query.
- **Contacts:** `<host-drivers.team@qti.qualcomm.com>` — For any question or comment regarding QUD driver.

---

## [1.00.96.0]
1. Test change to validate the automated develop -> main Windows CI/release pipeline (CI/release-automation testing, no functional driver changes).

## [1.00.95.6]
1. All Drivers: Add qdcfgUI driver logging tool.
2. Serial Driver: Support PID 0x9149.
3. RmNet Driver: Fix a crash that could occur when connecting a faulty USB device.

## [1.00.95.4]
1. All Drivers: Remove support for x86 architecture.
2. Serial Driver: Support persist power management settings in serial driver.

## [1.00.95.3]
1. Serial Driver: Fixes for Diag port re-enumeration.

## [1.00.94.12]
1. QDSS Driver: Friendly Name fixes for QDSS driver for maintaining uniqueness for same VID PID devices.

## [1.00.94.11]
1. Serial Driver: Added support for PID 9148.

## [1.00.94.2]
1. Serial Driver: Support dial-up connection on Windows.
2. Serial Driver: Revert Device description saving to registry.

## [1.00.94.1]
1. All Drivers: Fixes for Ring buffer support for Vi devices.
2. All Drivers: Support for Device description saving to registry.
3. All Drivers: Support for 0xD002 PID.

## [1.00.94.0]
1. All Drivers: This version is released with Open Source repository.

## [1.00.93.0]
1. All Drivers: Remove license check from installer.
2. All Drivers: Remove legacy components and drivers.
3. RmNet Driver 5.0.1.0: Support 5G cellular connection on Windows.
4. RmNet Driver 5.0.1.0: Remove 3rd party components.
5. RmNet Driver 5.0.1.0: Fix MBN download issues.

## [1.00.91.8]
1. QcAdb 1.0.1.6: Update adb device matching over usb while using multi UVC compositions.
2. Serial Driver 1.0.3.3: Fix an issue that crashes the system when opening/closing device handle too fast.

## [1.00.91.6]
1. All Drivers: Reduce and optimize the size of installed binaries.
2. QcAdb 1.0.1.5: Update device support for PIDs 9117, 9130, and 912F.
3. Filter Drivers 1.0.1.4: Fix an invalid memory access issue.
4. QDSS Driver 1.0.4.1: Remove support on Windows 7 and earlier versions.
5. Serial Driver 1.0.3.2: Fix an invalid memory access issue.
6. Serial Driver 1.0.3.2: Fix an error that cause system crash when transition to working state.
7. Serial Driver 1.0.3.2: Improve compatibility for build loading on Glymur devices.

## [1.00.91.3]
1. All Drivers: Fix driver signature.

## [1.00.91.2]
1. All Drivers: Optimize driver package size.
2. Serial Driver 1.0.3.1: Support new device PID 9145.
3. Serial Driver 1.0.3.1: Fix chip id format.
4. Serial Driver 1.0.3.1: Introduce new buffer mechanism to support larger transaction length.
5. Serial Driver 1.0.3.1: Fix an issue that cause NUL padding in read buffer.
6. Serial Driver 1.0.3.1: Improve compatibility with PuTTY.

## [1.00.90.9]
1. All Drivers: Support new device PIDs 9141, 9142, 9143, 9144.
2. All Drivers: Fix an issue in driver install script.
3. Serial Driver (WDF Framework) 1.0.2.9: Fix diag unresponsive issue.
4. Serial Driver (WDF Framework) 1.0.2.9: Fix system crash stop code KMODE_EXCEPTION_NOT_HANDLED.

## [1.00.90.5]
1. Serial drivers: Removed vulnerable APIs.
2. Serial drivers: Installer fixes.

## [1.00.90.4]
1. Serial Driver (WDF Framework) 1.0.2.7: Fix system crash when using Qualcomm VI Devices.
2. Serial Driver (WDF Framework) 1.0.2.7: Fix device doesn't enter low power state when using TeraTerm and PuTTy.

## [1.00.90.2]
1. All drivers: Support USB composition 0x9340 in serial, net.
2. All drivers: Support USB composition 0x913F in serial.
3. All drivers: Support USB composition 0x9140 in adb and serial.
4. All drivers: Fix for PIDs 0x913E and 0x913D.
5. All drivers: Removed driver selection window. WWAN and WDF Serial driver installed by default.
6. Serial Driver (WDF Framework) 1.0.2.5: Fix USB selective suspend on some configurations.

## [1.00.90.0]
1. All drivers: Support USB composition 0x9340 in serial, net.
2. All drivers: Support USB composition 0x913F in serial.
3. All drivers: Support USB composition 0x9140 in adb and serial.
4. All drivers: Fix for PIDs 0x913E and 0x913D.
5. All drivers: Removed driver selection window. WWAN and WDF Serial driver installed by default.

## [1.00.89.3]
1. All drivers: Unified ProductVersion and FileVersion for the driver binary.
2. All drivers: Support USB composition 0x9110 in adb.
3. All drivers: Support USB composition 0x913D in serial.
4. All drivers: Support USB composition 0x913E in adb and serial.

## [1.00.89.1]
1. All drivers: Unified ProductVersion and FileVersion for the driver binary.
2. Serial Driver (WDF Framework) 1.0.2.1: Fixed a deadlock issue that may cause system crash (DPC_WATCHDOG_VIOLATION).
3. Network driver 4.0.9.1: Restrict muxID range to 0x70 to 0x8F for tethered data call.

## [1.00.89.0]
1. Serial Driver (WDF): Added wait-on-mask feature.
2. Serial Driver (WDF): Added filter driver support PID 913C.
3. Serial Driver (WDF): Added support PID 912C and 912D for IPC Router.
4. Serial Driver (WDF): Updated version number delimiter.
5. Serial Driver (WDF): Support for SOCVER.

## [1.00.88.4]
1. Serial Driver (Both WDF and WDM Framework): Fixed an issue that may cause system crash when unloading the driver.
2. Serial Driver (Both WDF and WDM Framework): Support applications that use WAIT_ON_MASK IOCTL (Teraterm, etc.).

## [1.00.88.2]
1. Serial Driver (Both WDF and WDM Framework) / QCADB Driver / QDSS Driver: Support new DIAG device 0x913C.

## [1.00.88.1]
1. QUD Driver Installer 1.00.88.1: wdfserial is now default in QUD installer.

## [1.00.88]
1. Serial Driver (WDF Framework) 1.0.8.8: Fixed a 0xC9 BSOD causing by unprocessed system control request.
2. Serial Driver (WDF Framework) 1.0.8.8: Increased serial buffer to resolve application compatibility issue.

## [1.00.87]
1. Serial Driver (WDF Framework) 1.0.8.7: Added the QCPNP_EvtDeviceQueryRemoval entry point for handling device disable/enable from device manager.
2. Serial Driver (WDF Framework) 1.0.8.7: Added Wait-on-Mask non power managed queue.
3. Serial Driver (WDF Framework) 1.0.8.7: Added the Wait-on-Device-Removal non power managed queue.
4. Serial Driver (WDF Framework) 1.0.8.7: Changed the memory type to NonPagedPoolNx.
5. Serial Driver (WDF Framework) 1.0.8.7: Fixed handling the system control IRP correctly.

## [1.00.86]
1. Serial Driver (WDF Framework) 1.0.8.6: Fixed a race condition to complete the Read Request on cancellation in all scenarios.

## [1.00.85] - 2024-03-22
1. Serial Driver (WDF Framework) 1.0.0.4: Support system power management.
2. Serial Driver (WDF Framework) 1.0.0.4: Fixed the EMMCDL tool failing case.
3. Serial Driver (WDF Framework) 1.0.0.4: Fixed an issue that may cause unprocessed request.
4. Serial Driver (WDF Framework) 1.0.0.4: Fixed an issue that may cause indefinite wait of read request.

## [1.00.84] - 2024-02-07
1. Serial Driver (WDF Framework) 1.0.0.3 / Serial driver 2.1.5.5 / QDSS/DPL driver 1.0.3.3 / Filter driver 1.0.8.3 / QCADB driver 1.0.0.9: Added PIDs 9131, 9132, 9133, 9134, 9135 and 9136 support.
2. Serial Driver (WDF Framework) 1.0.0.3: Added support for set stamp.
3. Network driver 4.0.8.2: Fixes for WWAN driver for issue with Pakala target.

## [1.00.83] - 2024-02-02
1. General: Made old framework driver (qcusbser) as the default driver during silent install.
2. Serial Driver (WDF Framework) 1.0.0.2 / Serial driver 2.1.5.4 / QDSS/DPL driver 1.0.3.2 / Network driver 4.0.8.2 / Filter driver 1.0.8.2 / QCADB driver 1.0.0.8: version bump.

## [1.00.82] - 2024-01-03
1. Serial Driver (WDF Framework) 1.0.0.2: Fixed system crash issues.
2. Serial Driver (WDF Framework) 1.0.0.2 / Serial driver 2.1.5.4 / QDSS/DPL driver 1.0.3.2 / Network driver 4.0.8.2 / Filter driver 1.0.8.2 / QCADB driver 1.0.0.8: Added PIDs 0x9128 to 0x9130.

## [1.00.80] - 2023-10-05
1. Serial Driver (WDF Framework) 1.0.0.1: New Serial driver based on WDF Framework released.
2. Serial driver 2.1.5.3 / QDSS/DPL driver 1.0.3.1 / Network driver 4.0.8.0 / Filter driver 1.0.8.0 / QCADB driver 1.0.0.7: Updated version number.

## [1.00.79] - 2023-08-25
1. Caters all INF file date issue.

## [1.00.78] - 2023-08-24
1. Updated INF file date issue.

## [1.00.77] - 2023-07-11
1. Serial driver 2.1.5.2 / QDSS/DPL driver 1.0.3.1 / QCADB driver 1.0.0.6: Added PIDs 9128, 9129, 912A, and 912B support.

## [1.00.76] - 2022-12-08
1. General: Installer update - filter the Alpaca PID for uninstallation so that Alpaca drivers are not touched.
2. General: QdcfgUI - Added other driver configuration settings.
3. General: QdcfgUI - Make qdcfgUI available in command line.
4. Serial driver 2.1.5.1: Balanced en-queuing and de-queuing for pre-read queue.
5. Serial driver 2.1.5.1: Added feature to send SVEFlowEntry after 100ms after IRP_MN_START_DEVICE IRP completion.
6. Network driver 4.0.7.6: Added support for NDIS 6.30 support for ARM64.

## [1.00.73] - 2022-02-03
1. Serial driver 2.1.4.8: Fixed DFX BSOD issues.
2. Serial driver 2.1.4.8 / QDSS/DPL driver 1.0.2.9 / Network driver 4.0.7.3: Added CID retrieval from product string.

## [1.00.72] - 2021-10-28
1. Serial driver 2.1.4.7: Added support for viUSB.
2. Serial driver 2.1.4.7: Added PID 9FFE support.
3. Serial driver 2.1.4.7: Added L2 completion tracking.
4. Serial driver 2.1.4.7: Fixed PID 90A9 issue.
5. QDSS/DPL driver 1.0.2.8: Fixed DFx registry settings issue.
6. Network driver 4.0.7.2: Fixed PID 9067 issue.
7. Filter driver 1.0.7.2: Added more debug statements to track IRP completions.

## [1.00.71] - 2021-06-12
1. Serial driver 2.1.4.6: Added PIDs 9116 and 9117 support.
2. Serial driver 2.1.4.6: Added a fix for the L2CompletionList corruption.
3. Network driver 4.0.7.1: Fixed Queue corruption issue.
4. Filter driver 1.0.7.1: Added PIDs 9116 and 9117 support.
5. QCADB driver 1.0.0.4: Added PIDs 90FF and 9117 support.

## [1.00.70] - 2021-05-27
1. General: Added PnpLockDown flag to all INF files for compliance.
2. Serial driver 2.1.4.5: Fixed HLK compliance issue when device fails to stop.
3. Serial driver 2.1.4.5: Corrected device signatures for USB composition 90BF (INF only).
4. Serial driver 2.1.4.5: Removed IA64 target from INF.
5. QDSS/DPL driver 1.0.2.6: Corrected device signatures for USB composition 90BF (INF only).
6. Network driver 4.0.7.0: Fixed double locking issue.

## [1.00.68] - 2021-03-17
1. Serial driver 2.1.4.4: Added PID 9113, 9114 and 9115 support.
2. Serial driver 2.1.4.4: Added SVE flow entry feature.
3. QDSS/DPL driver 1.0.2.6: Added ETW logging support.
4. QCADB driver 1.0.0.3: Added PID 90E2, 9113 and 9115 support.

## [1.00.67] - 2021-01-13
1. Serial driver 2.1.4.3: Removed the registry entries to ignore USB serial number.
2. Network driver 4.0.6.7: Added the fix for the "radio off" issue with 1803 and above Win10 builds.
3. Network driver 4.0.6.7: Fix of possible double acquisition of TX spinlock.
4. QCADB driver 1.0.0.2: Added QCADB driver support.

## [1.00.66] - 2020-11-30
1. Serial driver 2.1.4.2: Made DFX support a compile time support since DFX support only from Win10 1903 build onwards.
2. QDSS/DPL driver 1.0.2.5: Changed device names to match MDMs correctly.
3. QCADB driver 1.0.0.2: Added QCADB driver support.

## [1.00.65] - 2020-10-01
1. Serial driver 2.1.4.1 / QDSS/DPL driver 1.0.2.3 / Network driver 4.0.6.4 / Filter driver 1.0.6.4: Added DFX support and PID 0x9112 support.

## [1.00.64] - 2020-05-27
1. Serial driver 2.1.4.0: Removed all the EUD devices support from QUD.
2. Serial driver 2.1.4.0: Made LPC feature internal.
3. Serial driver 2.1.4.0: Internal buffering tune-up.
4. Serial driver 2.1.4.0: Logging enrichment.
5. Filter driver 1.0.6.4: Removed all the EUD devices support from QUD.

## [1.00.63] - 2020-04-24
1. Network driver 4.0.6.3: Fixed crash issue due to ReleaseSpinLock.

## [1.00.62] - 2020-03-27
1. Serial driver 2.1.3.9 / QDSS/DPL driver 1.0.2.2 / Network driver 4.0.6.1 / Filter driver 1.0.6.1: Added PID 9108-9111 support.
2. Network driver 4.0.6.1: Added fix to decode ICCID correctly when the length returned for ICCID is odd number.
3. Network driver 4.0.6.1: Fixed qcusbnet driver crash issue due to NDIS changed behaviour in RX path.

## [1.00.61] - 2019-10-20
1. Serial driver 2.1.3.8 / QDSS/DPL driver 1.0.2.2 / Filter driver 1.0.6.1: Added PID 90FF, 9102-9107 support.
2. QDSS/DPL driver 1.0.2.2: Corrected alignment error in QDSS/DPL driver.
3. Network driver 4.0.6.1: Removed potential failure of access to device object during device removal.
4. Network driver 4.0.6.1: Added SSR support.
5. Network driver 4.0.6.1: Corrected imbalance of spinlock in RmNet read operation.
6. Network driver 4.0.6.1: Enhanced asynchronous operation of QMI messaging.
7. Network driver 4.0.6.1: Resolving race conditions among cancellation, completion and IO operation.

## [1.00.60] - 2019-05-29
1. Serial driver 2.1.3.7 / QDSS/DPL driver 1.0.2.1 / Network driver 4.0.6.0 / Filter driver 1.0.6.0: Added PID 90FC, 90FD and 90FE support.
2. Network driver 4.0.6.0: Fixed QMAPV4 support.

## [1.00.59] - 2019-05-24
1. Filter driver 1.0.5.8: Added ARM/ARM64 support.

## [1.00.58] - 2019-02-08
1. Serial driver 2.1.3.6: Added PIDs 90F3, 90F6, 90F7, 90F8, 90F9, 90FA and 90FB support.
2. QDSS/DPL driver 1.0.2.0: Added PIDs 90F6, 90F7, 90F8 and 90F9 support.
3. Network driver 4.0.5.8: Added QMAPV4 support for PID 90F1.
4. Network driver 4.0.5.8: Updated PID 90EA to PID 90F2 support for Win10.
5. Network driver 4.0.5.8 / Filter driver 1.0.5.8: Added PIDs 90F6, 90F7, 90F8, 90F9, 90FA and 90FB support.

## [1.00.57] - 2018-12-17
1. Serial driver 2.1.3.5 / QDSS/DPL driver 1.0.1.9 / Network driver 4.0.5.7 / Filter driver 1.0.5.7: Update to cleanup device protocol code.

## [1.00.56] - 2018-12-10
1. Serial driver 2.1.3.4 / Filter driver 1.0.5.6 / QDSS/DPL driver 1.0.1.8 / Network driver 4.0.5.6: Added PIDs 90ED, 90EE, 90EF, 90F0, 90F1 and 90F2.
2. Network driver 4.0.5.6: Enable 5G throughput settings by default.
3. Network driver 4.0.5.6: Priority adjustment for RX threads (USB L2/L1/MPRX/packet indication).
4. Network driver 4.0.5.6: Selective suspend/function suspend is disabled.
5. Network driver 4.0.5.6: Statistics enrichment to show active RX threads.
6. Network driver 4.0.5.6: Fixed QMAPv4 support issue for external released drivers.
7. Network driver 4.0.5.6: Fixed to look at AppType for reading IMSI of a SIM card.

## [1.00.55] - 2018-10-10
1. Serial driver 2.1.3.3 / Filter driver 1.0.5.5 / QDSS/DPL driver 1.0.1.7: Modified PID 90E2 support for UDE enumeration.
2. Network driver 4.0.5.5: Removed padding bytes for QMAPv4 aggregation.

## [1.00.54] - 2018-08-17
1. Network driver 4.0.5.4: DL/RX throughput optimization with multiple RX streams interfacing NDIS layer.
2. Network driver 4.0.5.4: RX optimization with "cluster" indication.
3. Network driver 4.0.5.4: Support UL aggregation configuration with new TLVs.
4. Network driver 4.0.5.4: Added enhanced network statistics to provide real-time status through custom IOCTL.
5. Network driver 4.0.5.4: Support MTU up to 10K.
6. Network driver 4.0.5.4: Consolidated settings for 5G throughput with single registry entry (QCMPEnableData5G).
7. Network driver 4.0.5.4: Updated driver config tool (qdcfg) to support QCMPEnableData5G.
8. Network driver 4.0.5.4: Defined default settings for 5G throughput.
9. Network driver 4.0.5.4: Changed default number of aggregated UL packets from 15 to 21 (1500 * 21 is around 31K).

## [1.00.53] - 2018-07-05
1. Serial driver 2.1.3.2 / Network driver 4.0.5.3: Added PID 90E2 support for UDE enumeration.
2. Serial driver 2.1.3.2 / Network driver 4.0.5.3 / Filter driver 1.0.5.3 / QDSS/DPL driver 1.0.1.6: Added Multi config 90EA, 90EB PIDs support and PID 90EC support.
3. Network driver 4.0.5.3: Added QMAPv4 support.
4. Serial driver 2.1.3.2: Added device removal/add in device power D2 state.

## [1.00.52] - 2018-02-21
1. Serial driver 2.1.3.1 / Network driver 4.0.5.2 / Filter driver 1.0.5.2 / QDSS/DPL driver 1.0.1.5: Added PID 90E2 - 90E9.
2. Serial driver 2.1.3.1 / Network driver 4.0.5.2: Reworked USB super-speed function suspend.
3. Serial driver 2.1.3.1: Added support for device removal under low device power state.
4. Network driver 4.0.5.2: Removed double event initialization to avoid potential race condition causing device removal to hang.
5. Network driver 4.0.5.2: Added faking IMSI and ICCID for testing.

## [1.00.51] - 2017-12-21
1. Serial driver 2.1.3.0 / Network driver 4.0.5.1 / Filter driver 1.0.5.1: Added support for composition 90DD, 90DE, 90DF, 90E0, 90E1.
2. Serial driver 2.1.3.0 / Network driver 4.0.5.1: Added the ability of retrieving device serial number with arbitrary length.
3. Serial driver 2.1.3.0: Enhanced device arrival notification mechanism.
4. Filter driver 1.0.5.1: Added customization of device friendly name.
5. QDSS/DPL driver 1.0.1.4: Added support for composition 90DD, 90DE and the ability of retrieving device serial number with arbitrary length.
6. Network driver 4.0.5.0: Added packet detach for OID_WWAN_PACKET_SERVICE (Query OID).
7. Filter driver 1.0.4.9: Made changes to balance calls on RmLoc.

## [1.00.49] - 2017-09-27
1. Build environment/procedure migrated to Visual Studio 2015 and WDK10.
2. Build to meet Windows 10 requirements.
3. Serial driver 2.1.2.9 / Network driver 4.0.4.9 / Filter driver 1.0.4.9 / QDSS/DPL driver 1.0.1.4: Added support for composition 90DA, 90DB, 90DC.
4. Serial driver 2.1.2.9 / QDSS/DPL driver 1.0.1.4: Corrected definition for composition 90C0.
5. Network driver 4.0.4.9: Added QMI_NAS_SET_SYSTEM_SEL_PREF for packet attach/detach.
6. QDSS/DPL driver 1.0.1.4: Enforced customization of device name in user space.

## [1.00.47] - 2017-08-03
1. Serial driver 2.1.2.7 / Network driver 4.0.4.7 / QDSS/DPL driver 1.0.1.3: Added support for ADB serial number.
2. Serial driver 2.1.2.7: Added support for composition 90D9.
3. Serial driver 2.1.2.7: Added tracking for power settings.
4. Network driver 4.0.4.7: Increased RX timeout for QMI CTL service to accommodate slower device.
5. Filter driver 1.0.4.7: Added support for composition 90D9.

## [1.00.46] - 2017-07-07
1. Serial driver 2.1.2.6: Renamed some of the device names in INF file.
2. Network driver 4.0.4.6: Fixed the IMSI issue when UIM_READ_TRANSPARANT returns non correct IMSI format.
3. Network driver 4.0.4.6: Fixed passing correct TLV info for SET_SYS_SELECTION.
4. Filter driver 1.0.4.6: Added 90CA, 90CB, 90CC, 90CD, 90D0, 90D1, 90D2, 90D3, 90D4, 90D5 and 90D6 support.
5. Filter driver 1.0.4.6 / QDSS/DPL driver 1.0.1.2: Added NonPagedPoolNx support to pass WHCK on Win8 and later OS.
6. Filter driver 1.0.4.6: Added support for POOL_NX_OPTIN.
7. QDSS/DPL driver 1.0.1.2: Added new Qualcomm Class support.
8. QDSS/DPL driver 1.0.1.2: Refined retrieval of device serial number.
9. QDSS/DPL driver 1.0.1.2: Added support for custom protocol tied to each function.

## [1.00.45] - 2017-03-02
1. Serial driver 2.1.2.5 / Network driver 4.0.4.5 / Filter driver 1.0.4.6: Added 90CA, 90CB, 90CC, 90CD, 90D0, 90D1, 90D2, 90D3, 90D4, 90D5 and 90D6 support.
2. Serial driver 2.1.2.5 / Network driver 4.0.4.5: Added NonPagedPoolNx support to pass WHCK on Win8 and later OS.
3. Serial driver 2.1.2.5 / Network driver 4.0.4.5: Refined retrieval of device serial number.
4. Serial driver 2.1.2.5 / Network driver 4.0.4.5: Added support for custom protocol tied to each function.
5. Serial driver 2.1.2.5 / Network driver 4.0.4.5 / Filter driver 1.0.4.6: Added support for POOL_NX_OPTIN.
6. QDSS/DPL driver 1.0.1.1: Added new Qualcomm Class support.
7. QDSS/DPL driver 1.0.1.1: Added NonPagedPoolNx support to pass WHCK on Win8 and later OS.
8. QDSS/DPL driver 1.0.1.1: Refined retrieval of device serial number.
9. QDSS/DPL driver 1.0.1.1: Added support for custom protocol tied to each function and POOL_NX_OPTIN.

## [1.00.44] - 2016-10-31
1. Network driver 4.0.4.4: Added deregistration of serving_system indication if sys_info is supported.
2. Network driver 4.0.4.4: Added registration of sys_info indication if sys_info is supported.
3. Filter driver 1.0.4.4: Added correct return code from dispatch routine.

## [1.00.43] - 2016-09-23
1. Network driver 4.0.4.3: Added registry setting to enable Signal Strength disconnect timer.

## [1.00.42] - 2016-08-17
1. Network driver 4.0.4.1: Added the PACKET_SERVICE Detach spoof for LTE with respect to NAS_GET_SYS_INFO.
2. Filter driver 1.0.4.2: Fixed the DebugPrint BSOD issue.

## [1.00.41] - 2016-07-26
1. Serial driver 2.1.2.3 / Network driver 4.0.4.1 / Filter driver 1.0.3.2: Added support for PID 90C5, 90C6, 90C7, 90C8 and 90C9.
2. Network driver 4.0.4.1: Added the fix not to drop the packets when the remaining aggregation size reaches the exact size of the packet.
3. Filter driver 1.0.3.2: Added the fix to sync the remove lock destruction.
4. Filter driver 1.0.3.2: Fixed IRP_MN_START_DEVICE handling, the handler waits for the event always.
5. QDSS/DPL driver 1.0.0.9: Added support for 90C8 and 90C9.

## [1.00.40] - 2016-05-10
1. Serial driver 2.1.2.2: Added support for PID 90BF and 90C0, 90C1, 90C2, 90C3.
2. Serial driver 2.1.2.2: Added a timeout so that ReadThread can be canceled if start device fails.
3. Serial driver 2.1.2.2: Consolidated the initialization of write queues with other queue initializations to avoid access failure if USB fails during enumeration.
4. Network driver 4.0.4.0: Added feature to signal LINK_STATE indication when signal strength drops to 0.
5. Network driver 4.0.4.0: Added a guard for event creation with DEVICE_REMOVAL_EVENTS_MAX.
6. Network driver 4.0.4.0: Fixed the memory leak issue with RtlAnsiStringToUnicodeString.
7. Network driver 4.0.4.0 / Filter driver 1.0.3.1 / QDSS/DPL driver 1.0.0.9: Added support for PID 90C1, 90C2, 90C3 and PID 90BF and 90C0.
8. Network driver 4.0.4.0: Added the fix for decoding the ICCID correctly.
9. Filter driver 1.0.3.1: Added remove lock acquiring for IRP_MJ_WRITE, IRP_MJ_CLEANUP and IRP_MJ_DEVICE_CONTROL.
10. Filter driver 1.0.3.1: Clear Dispatch table for initializing.
11. QDSS/DPL driver 1.0.0.9: Fixed DPL stability issue during device removal.

## [1.00.39] - 2015-11-17
1. Serial driver 2.1.2.1: version bump.
2. Network driver 4.0.3.9: Replaced deprecated DMS_UIM messages with UIM QMI messages.
3. Network driver 4.0.3.9: Replaced deprecated QMI_NAS messages with other NAS QMI messages.

## [1.00.38] - 2015-10-08
1. General: Added the rogue driver cleanup in Installer.
2. General: Added support for configuring QMI request threshold.
3. Serial driver 2.1.2.1: Made changes so that an ACM device enumerated under Port class.
4. Serial driver 2.1.2.1: Added IOCTL for retrieving parent device ID.
5. Serial driver 2.1.2.1: Added error handling to facilitate device removal when device disconnects during the process of port opening.
6. Serial driver 2.1.2.1: Made changes to speed up the cancellation of read threads.
7. Network driver 4.0.3.8: Added handling for RX buffer allocation failure.
8. Network driver 4.0.3.8: Added new feature to support Fuzzing test.
9. Network driver 4.0.3.8: Made the threshold of pending QMI requests configurable.
10. Network driver 4.0.3.8: Added support for device/parent ID retrieval and IOCTLs for primary adapter name, parent device ID, and fuzzing test.
11. Network driver 4.0.3.8: Added the tx_id fix to always start from 0x01.
12. Filter driver 1.0.3.0: Added correct check for size when handling IOCTL_QCDEV_REQUEST_DEVICEID.
13. QDSS/DPL driver 1.0.0.7: Added IOCTL for retrieving parent device ID.
14. QDSS/DPL driver 1.0.0.7: Removed explicit cancellation of internal requests for DPL when receiving requests from applications, to avoid a potential timing issue at bus layer.

## [1.00.37] - 2015-07-08
1. Serial driver 2.1.1.9 / Network driver 4.0.3.7: Added support for device name pairing between DIAG and network adapter.
2. Serial driver 2.1.1.9 / Network driver 4.0.3.7: Added support for USB 3.0 function suspend.
3. Filter driver 1.0.2.9: Added support for device name pairing between DIAG and network adapter.

## [1.00.36] - 2015-05-27
1. General: Updated the installer to sign the drivers with VeriSign cross certificate.
2. General: Removed installation of test certificate and stopped turning on testsigning.
3. Serial driver 2.1.1.9 / Network driver 4.0.3.6 / Filter driver 1.0.2.8: Added PID 90B9.
4. Network driver 4.0.3.6: Fixed the remove lock issue.
5. Network driver 4.0.3.6: Added a fix to clean up TX queue when the system is put to S3/S4.

## [1.00.35] - 2015-04-10
1. Serial driver 2.1.1.8: Made changes to initialize PnP IRP with STATUS_NOT_SUPPORTED.
2. Serial driver 2.1.1.8: Handle IRP_MN_QUERY_REMOVE_DEVICE even when the device object is open.
3. Network driver 4.0.3.5: Added burst support for LTE.
4. Network driver 4.0.3.5: Made changes to initialize PnP IRP with STATUS_NOT_SUPPORTED.
5. Network driver 4.0.3.5: Added dual-IP support in QoS.
6. Network driver 4.0.3.5: Added runtime MTU support.
7. Network driver 4.0.3.5: Added fix for IP address issue after SSR.
8. Network driver 4.0.3.5: Added registry setting to disable QMAP FC.

## [1.00.34] - 2015-01-22
1. Network driver 4.0.3.4: Added unconditional wait for IRP completion from filter driver.
2. QDSS/DPL driver 1.0.0.6: Added fix that was causing device removal/driver update to get stuck forever.

## [1.00.33] - 2015-01-14
1. Network driver 4.0.3.3: Removed opening PDS client in the driver.
2. Filter driver 1.0.2.7: Fixed the BSOD issue caused when processing IRP_MJ_READ.

## [1.00.32] - 2014-12-05
1. Serial driver 2.1.1.7 / Network driver 4.0.3.2 / Filter driver 1.0.2.6 / QDSS/DPL driver 1.0.0.5: Added PIDs 90B7 and 90B8 support.
2. Network driver 4.0.3.2: Added registry settings for QCMPBindIFId, QCMPBindEPType and QCMPMuxId.
3. Network driver 4.0.3.2: Made sure all IRP allocations are allocated with count of device object stack locations.
4. Network driver 4.0.3.2: Added IOCTL_QCDEV_QMI_READY IOCTL to notify when the QMI is ready.
5. Network driver 4.0.3.2: Added extended IP-config support for IPV4 client.
6. Network driver 4.0.3.2: Removed timeout in release-client-id-response for performance.
7. Network driver 4.0.3.2: Reset PendingCTL requests count on QMI initialization.
8. Network driver 4.0.3.2: Fixed the Aggregation Count issue for MBIM aggregation.

## [1.00.31] - 2014-10-17
1. Serial driver 2.1.1.6: version bump.
2. Network driver 4.0.3.1: Re-worked notifications for MTU service so that notifications are cancelled properly with IRP_MJ_CLEANUP.
3. Network driver 4.0.3.1: MTU service is enforced on QMI CTRL device for removal notification registration.

## [1.00.30] - 2014-10-15
1. Serial driver 2.1.1.6 / Network driver 4.0.3.0 / Filter driver 1.0.2.5 / QDSS/DPL driver 1.0.0.4: Added 90B0, 90B2, 90B3, 90B4, 90B5 and 90B6 PIDs support.

## [1.00.29] - 2014-08-22
1. Serial driver 2.1.1.5 / Network driver 4.0.2.9 / Filter driver 1.0.2.4 / QDSS/DPL driver 1.0.0.3: Added 90AD, 90AE and 90AF PIDs support.
2. Network driver 4.0.2.9: Do not reset the MUXID if the BIND_MUX_DATA_PORT times out.
3. Network driver 4.0.2.9: Added the feature to support 8 byte QOS header.
4. Network driver 4.0.2.9: Added the QMAP DL minimum padding support.
5. Network driver 4.0.2.9: Added IOCTL_QCDEV_DEVICE_GROUP_INDETIFIER to return the rmnet grouping identifier.
6. Network driver 4.0.2.9: Fixed the PACKET_SERVICE Detach spoof for LTE.
7. Filter driver 1.0.2.4: Added Remove Lock to protect IRP management.
8. QDSS/DPL driver 1.0.0.3: Added DPL feature support.

## [1.00.27] - 2014-07-18
1. Serial driver 2.1.1.3 / Network driver 4.0.2.7 / Filter driver 1.0.2.2 / QDSS driver 1.0.0.1: Added 90AC PID support.
2. Serial driver 2.1.1.3: Fixed the completion of WAIT_ON_MASK IRP in PnP surprise removal event.
3. Network driver 4.0.2.7: Reset the QMAP Flow control flags when the call is disconnected.
4. Network driver 4.0.2.7: Reset the MUXID if the QMIWDS_BIND_MUX_DATA_PORT fails to support targets that do not support binding.

## [1.00.26] - 2014-06-27
1. Serial driver 2.1.1.2: version bump.
2. Network driver 4.0.2.6: Added feature for extended IP configuration.
3. Network driver 4.0.2.6: Fixed the QMI Sync for MUX adapters.
4. Network driver 4.0.2.6: Added more checks to discard badly formatted packets.
5. Network driver 4.0.2.6: Added QMUX Bind feature support for QOS and DFS clients.
6. Network driver 4.0.2.6: Added 90AA and 90AB PIDs support.
7. Filter driver 1.0.2.1 / QDSS driver 1.0.0.0: version bump.

## [1.00.25] - 2014-04-17
1. Network driver 4.0.2.5: Remove the QMIInit for Mux enabled in MPInitialize to support quicker enumeration.

## [1.00.24] - 2014-04-08
1. Serial driver 2.1.1.2 / Network driver 4.0.2.4 / Filter driver 1.0.2.1: Added PID 90A9 support.
2. Network driver 4.0.2.4: Fixed the OID Initialization sync up after QMI Initialization.

## [1.00.23] - 2014-03-18
1. Serial driver 2.1.1.1 / Network driver 4.0.2.3 / Filter driver 1.0.2.0: Added PIDs 90A7, 90A8 support.
2. Network driver 4.0.2.3: Added the variable MTU size support to the installer.

## [1.00.22] - 2014-02-20
1. Network driver 4.0.2.2: Increased the default number of NDIS buffers to 200.
2. Network driver 4.0.2.2: Added a scheduling delay when the NDIS buffers reach 0.

## [1.00.21] - 2014-01-25
1. Serial driver 2.1.1.0 / Network driver 4.0.2.1 / Filter driver 1.0.1.9: Added PIDs 9098, 9099, 909A, 909B, 909C, 909D, 909E, 909F, 90A0, 90A1, 90A2, 90A3, 90A5 and 90A6 support.
2. Network driver 4.0.2.1 / Filter driver 1.0.1.9: Added MUXing feature support for 9035, 908F and 90A1 PIDs.
3. Network driver 4.0.2.1: Changed Number of L2 Buffers to 64 for Super Speed USB.
4. Network driver 4.0.2.1: By default the Number of DL Aggregation parameters are set to 32 (packets) and 32K (size).
5. Network driver 4.0.2.1: Added QMIWDS_ADMIN_SET_QMAP_SETTINGS_REQ for enabling QMAP Flow control.
6. Network driver 4.0.2.1: Disable QMAPV3 and QMAPV2 by default.
7. Network driver 4.0.2.1: Change the default number of TLP buffers to 64 if the UL aggregation count is 1.

## [1.00.20] - 2013-12-11
1. Serial driver 2.1.0.9: Added processing for IOCTL_QCBUS_SEND_CONTROL support.
2. Serial driver 2.1.0.9 / Network driver 4.0.2.0 / Filter driver 1.0.1.8: Added PIDs 908E, 908F, 9090, 9091, 9092, 9093, 9094, 9095, 9096 and 9097 support.
3. Network driver 4.0.2.0: Added QMAP V3 support.
4. Network driver 4.0.2.0: Added the QMI support for Bind MuxId Data Port support.
5. Network driver 4.0.2.0: Added support for handling QMAP Fragmented IP packets.
6. Network driver 4.0.2.0: Added the variable MTU size support.

## [1.00.19] - 2013-08-14
1. Serial driver 2.1.0.8 / Network driver 4.0.1.9 / Filter driver 1.0.1.7: Added PID 9049 support.
2. Network driver 4.0.1.9: Added the support to clear IP setting for NDIS 6.20 driver when the call drops.
3. Network driver 4.0.1.9: Added the MuxId configuration support.
4. Network driver 4.0.1.9: Enabled MpQuickTx for transmits to be more faster.

## [1.00.18] - 2013-07-11
1. Network driver 4.0.1.8: Added the support for QMAP Flow control feature.
2. Network driver 4.0.1.8: Fixed the crash when QOS is enabled in WWAN driver.
3. Network driver 4.0.1.8: Added QMI WDA UL aggregation parameters support.
4. Network driver 4.0.1.8: Added the IOCTL IOCTL_QCDEV_INJECT_PACKET support for injecting error packets.

## [1.00.17] - 2013-05-24
1. Network driver 4.0.1.7: Set the default Max aggregated packets to 15.
2. Network driver 4.0.1.7: Added the support for decoding chained QCNCM NDP aggregated packets.
3. Network driver 4.0.1.7: Negotiate TLP aggregation when MBIM UL/DL is disabled in registry.

## [1.00.16] - 2013-05-07
1. Serial driver 2.1.0.7 / Network driver 4.0.1.6 / Filter driver 1.0.1.6: Added PIDs 908A and 908B support.
2. Network driver 4.0.1.6: Added Disable QMI support.
3. Network driver 4.0.1.6: Added the QMI pending queue to hold QMI requests till QMI is not initialized.
4. Network driver 4.0.1.6: Fixed the race condition between queuing and processing TX packets (NDIS 6.20).

## [1.00.15] - 2013-04-23
1. Serial driver 2.1.0.6 / Filter driver 1.0.1.5: Added PIDs 9086 to 9089 support.
2. Network driver 4.0.1.5: Fixed the WHCK issue regarding the RegisterState.

## [1.00.13] - 2013-04-02
1. Serial driver 2.1.0.6: Removed the check to prevent unbalanced removelock when port open/close happens.
2. Serial driver 2.1.0.6 / Network driver 4.0.1.3 / Filter driver 1.0.1.3: Added PIDs 9079 to 9085 support.
3. Serial driver 2.1.0.6: Fixed the Read Buffer Management in the wrapped around scenarios.
4. Network driver 4.0.1.3: Added registry configuration for L2 read buffers.
5. Network driver 4.0.1.3: Added registry configuration setting the thread priority.
6. Network driver 4.0.1.3: Fixed the Read IRP completion when the states is not STATUS_PENDING.
7. Network driver 4.0.1.3: Made the L2 read non indexed read to accommodate more L2 read IRPs.
8. Network driver 4.0.1.3: Added QMAP aggregation support.

## [1.00.12] - 2012-12-06
1. Serial driver 2.1.0.5: Corrected PID 9068-76 support on modem INF file.
2. Serial driver 2.1.0.5 / Network driver 4.0.1.2 / Filter driver 1.0.1.2: Added PID 9077 and 9078 support.
3. Serial driver 2.1.0.5: Added the correct check for ioctl IOCTL_QCUSB_SET_DBG_UMSK.
4. Network driver 4.0.1.2: Corrected Selective Suspend Idle time registry setting.

## [1.00.11] - 2012-10-31
1. Serial driver 2.1.0.4 / Network driver 4.0.1.1: Added PIDs 9064-76 support.
2. Serial driver 2.1.0.4 / Network driver 4.0.1.1: Ignored the check for number of USB configurations.
3. Network driver 4.0.1.1: Added the registry configuration for disabling Timer resolution.
4. Network driver 4.0.1.1: Fixed the QCNCM aggregation logic and fixed BusyTx for MBB driver.
5. Network driver 4.0.1.1: Increased the aggregation count and fixed the count not to cross the limit.
6. Network driver 4.0.1.1: Added feature for handling simultaneous QMI clients opening at the same time.
7. Filter driver 1.0.1.1: Added PIDs 9064-76 support.

## [1.00.10] - 2012-10-23
1. Serial driver 2.1.0.2 / Network driver 4.0.1.0 / Filter driver 1.0.1.0: Added PIDs 9065 and 9066 support.
2. Network driver 4.0.1.0: Added registry configuration for Max number of aggregated packets.
3. Network driver 4.0.1.0: Added registry configuration to send Mode only LPM (deregistration only when the device is bus powered).
4. Network driver 4.0.1.0: Calling GET_PKT_SRVC after QMI initialization to get around the missed PKT_SRVC indication.

## [1.00.09] - 2012-09-07
1. Serial driver 2.1.0.0: Added PID 904A, 9060, 9061 and 9062 support.
2. Serial driver 2.1.0.0: Added retries for resetting pipes.
3. Serial driver 2.1.0.0: Recycle the L2 buffers when the device is being removed.
4. Network driver 4.0.0.9 / Filter driver 1.0.0.9: Added PID 9062 support.
5. Network driver 4.0.0.9: Added the interrupt pipe sharing feature.
6. Network driver 4.0.0.9: Fixed the aggregation transmit feature for ndis 6.20 driver.
7. Network driver 4.0.0.9: Added the request to get the QMI versions for IOCTL_QCDEV_QMI_GET_SVC_VER_EX instead of returning from the cache.

## [1.00.08] - 2012-08-25
1. Serial driver 2.0.9.9: Fixed L2CompletionQueue corruption.

## [1.00.07] - 2012-08-08
1. Serial driver 2.0.9.8: Added PIDs 9059, 905A, 905D, 905E, 905F support.
2. Serial driver 2.0.9.8 / Filter driver 1.0.0.8: Added Aggregation support.
3. Serial driver 2.0.9.8: Added runtime debug settings.

## [1.00.06] - 2012-06-15
1. Serial driver 2.0.9.7: Moved device interface registration to StartDevice.
2. Serial driver 2.0.9.7: Made changes to handle out-of-order removal event.
3. Network driver 4.0.0.6: Initialization of NdisMediumType to NdisMediumWirelessWan irrespective of IP mode setting.
4. Network driver 4.0.0.6: Changed TLP Max size to 128*1024.
5. Network driver 4.0.0.6: Added the Transmit Timer for Aggregation.
6. Filter driver 1.0.0.7: Added PIDs 3197, 3199, 319A, 319B, 3200, 6000, 8002, 9002, 9004, 9006, 9012, 9013, 9016, 9017, 9018, 9019, 901B, 901C, 901D, 901F, 9020, 9028, 9029, 902C, 902D, 902F, 9030, 903A, 903F, 9040, 9041, 9042, 9044, 9045, 9100, 9101, 9402, 9404.

## [1.00.05] - 2012-05-28
1. Serial driver 2.0.9.6 / Network driver 4.0.0.5 / Filter driver 1.0.0.6: Added PID 9056 support.

## [1.00.04] - 2012-05-17
1. Serial driver 2.0.9.5 / Network driver 4.0.0.4 / Filter driver 1.0.0.5: Added PID 9053, 9054 and 9055 support.
2. Serial driver 2.0.9.5 / Network driver 4.0.0.4: Added SelectiveSuspendIdleTime in Milliseconds.
3. Serial driver 2.0.9.5: Stop creating L2 read thread when the dev state is in REMOVED0 state.
4. Serial driver 2.0.9.5: Succeed IRP_MJ_CREATE/IRP_MJ_WRITE without doing anything when the dev state is in REMOVED0 state.
5. Network driver 4.0.0.4: Added registry configuration of SelectiveSuspendIdleTime value.
6. Network driver 4.0.0.4: Added support for configuring MTU Size.
7. Network driver 4.0.0.4: Added TLP DL configuration support.
8. Network driver 4.0.0.4: Added DUAL IP flow control.
9. Network driver 4.0.0.4: Removed the TLP header validation.

## [1.00.03] - 2012-04-02
1. Serial driver 2.0.9.4 / Filter driver 1.0.0.4: Added support for PID 904F, 9050, 9051 and 9052.
2. Network driver 4.0.0.3: Added support for PID 9050 and 9052.

## [1.00.02] - 2012-03-13
1. Serial driver 2.0.9.3: Fixed the bug which could cause queue corruption during device removal.
2. Serial driver 2.0.9.3 / Network driver 4.0.0.2: Added support for PID 904B and 904C.
3. Serial driver 2.0.9.3: Corrected definition for PID F005.
4. Serial driver 2.0.9.3: Added registry settings so that USB serial number is ignored for specific PIDs.
5. Serial driver 2.0.9.3: Take out options to read multi read/write registry values.
6. Network driver 4.0.0.2: Added DL control feature for internal debugging.
7. Network driver 4.0.0.2: Increased number of I/O buffers to accommodate the bursty I/O characteristics of certain targets.
8. Network driver 4.0.0.2: Refined QMI initialization to block external clients until QMI is fully initialized.
9. Network driver 4.0.0.2: Added "QCIgnoreErrors" registry item and also added a check to ignore STATUS_INVALID_PARAMETER for QMI ctl write.
10. Network driver 4.0.0.2: Return FriendlyName for OID_GEN_VENDOR_DESCRIPTION.
11. Network driver 4.0.0.2: Made changes to dynamically determine number of TX buffers based on data aggregation state (on/off).
12. Filter driver 1.0.0.3: Added support for PID 904B and 904C.

## [1.00.00] - 2012-01-17
1. Serial driver 2.0.8.9 / Network driver 4.0.0.1: Updated all gobi changes.
