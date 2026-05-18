// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause

#include "main.h"
#include "registry.h"
#include "../../qcversion.h"

constexpr const wchar_t *COMMAND_QDCLR = L"qdclr.exe";
#ifdef _WIN64
constexpr const wchar_t *COMMAND_PNPUTIL_MAIN = L"pnputil /add-driver \"";
#else
constexpr const wchar_t *COMMAND_PNPUTIL_MAIN = L"C:\\Windows\\Sysnative\\pnputil.exe /add-driver \"";
#endif
constexpr const wchar_t *COMMAND_PNPUTIL_OPTS = L"\" /install /force";
constexpr const wchar_t *PATH_INF_DIR         = L".\\Drivers\\Windows10\\";

DWORD scan_for_hardware_changes()
{
    DEVINST dev_root = 0;
    CONFIGRET cr = CM_Locate_DevNode(&dev_root, NULL, CM_LOCATE_DEVNODE_NORMAL);

    if (cr != CR_SUCCESS)
    {
        printf("ERROR: CM_Locate_DevNode failed (CR=0x%X)\n", cr);
        return ERROR_DEVICE_ENUMERATION_ERROR;
    }

    cr = CM_Reenumerate_DevNode(dev_root, 0);
    if (cr != CR_SUCCESS)
    {
        printf("ERROR: CM_Reenumerate_DevNode failed (CR=0x%X)\n", cr);
        return ERROR_DEVICE_ENUMERATION_ERROR;
    }

    return ERROR_SUCCESS;
}

DWORD execute_command(const std::wstring &command)
{
    PROCESS_INFORMATION pi = {};
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    DWORD ret = ERROR_SUCCESS;

    if (!CreateProcessW(nullptr, const_cast<wchar_t *>(command.c_str()), nullptr,
                        nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
    {
        ret = GetLastError();
    }
    else
    {
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &ret);

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    return ret;
}

DWORD install_drivers()
{
    // Clean old drivers (best effort — ignore errors from qdclr)
    printf("\nRemoving old drivers ...\n");
    execute_command(COMMAND_QDCLR);

    // Find and install each .inf in current directory
    DWORD ret = ERROR_SUCCESS;
    WIN32_FIND_DATAW fd;
    std::wstring search_pattern = std::wstring(PATH_INF_DIR) + L"*.inf";
    HANDLE hFind = FindFirstFileW(search_pattern.c_str(), &fd);

    if (hFind == INVALID_HANDLE_VALUE)
    {
        printf("WARNING: no .inf files found in %ws\n", PATH_INF_DIR);
        return ERROR_FILE_NOT_FOUND;
    }

    do {
        std::wstring cmd = COMMAND_PNPUTIL_MAIN;
        cmd += PATH_INF_DIR;
        cmd += fd.cFileName;
        cmd += COMMAND_PNPUTIL_OPTS;
        printf("\nInstalling %ws ...\n", fd.cFileName);
        if ((ret = execute_command(cmd)) != ERROR_SUCCESS)
        {
            printf("ERROR: pnputil failed for %ws (exit code 0x%lX)\n", fd.cFileName, ret);
            FindClose(hFind);
            return ret;
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
    return scan_for_hardware_changes();
}

DWORD uninstall_drivers()
{
    printf("Removing drivers ...\n");
    DWORD ret = execute_command(COMMAND_QDCLR);

    if (ret == ERROR_FILE_NOT_FOUND)
    {
        printf("ERROR: qdclr.exe not found in current directory\n");
    }
    else if (ret == ERROR_ACCESS_DENIED)
    {
        printf("ERROR: qdclr.exe cannot be opened\n");
    }
    else if (ret == ERROR_SUCCESS)
    {
        ret = scan_for_hardware_changes();
    }
    else
    {
        printf("ERROR: qdclr.exe failed (exit code 0x%lX)\n", ret);
    }

    return ret;
}

static std::wstring get_exe_directory()
{
    std::wstring path(MAX_PATH, L'\0');
    DWORD len = GetModuleFileNameW(NULL, &path[0], static_cast<DWORD>(path.size()));
    path.resize(len);
    size_t pos = path.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? path.substr(0, pos) : L".";
}

static void inline print_usage()
{
    printf
    (
        "Usage:\n"
        "  qdinstall.exe -i\n"
        "  qdinstall.exe -x\n"
        "  qdinstall.exe -v\n"
    );
}

static DWORD parse_args(int argc, wchar_t *argv[], Options &opts)
{
    for (int i = 1; i < argc; i++)
    {
        std::wstring arg = argv[i];

        if (arg == L"-i")
        {
            opts.install = true;
            opts.uninstall = false;
        }
        else if (arg == L"-x")
        {
            opts.uninstall = true;
            opts.install = false;
        }
        else if (arg == L"-v")
        {
            opts.version = true;
        }
        else if (arg == L"-g")
        {
            opts.getInstallPath = true;
        }
        else if (arg == L"-p")
        {
            if ((i + 1) >= argc)
            {
                printf("ERROR: invalid installation path\n");
                return ERROR_INVALID_PARAMETER;
            }
            opts.installationPath = argv[++i];
            while (!opts.installationPath.empty() &&
                   (opts.installationPath.back() == L'\\' || opts.installationPath.back() == L'/'))
                opts.installationPath.pop_back();
            if (opts.installationPath.empty())
            {
                printf("ERROR: invalid installation path\n");
                return ERROR_INVALID_PARAMETER;
            }
            DWORD attr = GetFileAttributesW(opts.installationPath.c_str());
            if (attr == INVALID_FILE_ATTRIBUTES)
            {
                printf("ERROR: installation path does not exist\n");
                return ERROR_PATH_NOT_FOUND;
            }
            if (!(attr & FILE_ATTRIBUTE_DIRECTORY))
            {
                printf("ERROR: installation path is not a directory\n");
                return ERROR_DIRECTORY;
            }
        }
        else
        {
            printf("ERROR: unknown option: %ws\n", argv[i]);
            return ERROR_INVALID_PARAMETER;
        }
    }

    return ERROR_SUCCESS;
}

int wmain(int argc, wchar_t *argv[])
{
    Options opts;
    DWORD ret = parse_args(argc, argv, opts);
    if (ret != ERROR_SUCCESS)
    {
        print_usage();
        return ret;
    }

    if (opts.version)
    {
        printf("Package version:   %ws\n", QCOM_USB_DRIVERS_PRODUCT_VERSION_STRING_W);
        printf("Installed version: %ws\n", get_registered_install_version().c_str());
        return ERROR_SUCCESS;
    }

    if (opts.getInstallPath)
    {
        std::wstring location = get_registered_install_location();
        if (location.empty())
        {
            printf("%ws\n", L"no driver installation found");
            return ERROR_FILE_NOT_FOUND;
        }
        printf("%ws\n", location.c_str());
        return ERROR_SUCCESS;
    }

    // Set working directory to exe's own directory
    std::wstring exe_dir = get_exe_directory();
    if (!SetCurrentDirectoryW(exe_dir.c_str()))
    {
        ret = GetLastError();
        printf("ERROR: cannot set directory to '%ws' (0x%lX)\n", exe_dir.c_str(), ret);
        return ret;
    }

    if (opts.install)
    {
        ret = install_drivers();
        if (ret == ERROR_ACCESS_DENIED)
        {
            printf("ERROR: failed to install driver (admin required)\n");
            return ret;
        }
        if (ret == ERROR_NO_MORE_ITEMS)
        {
            printf("INFO: driver already installed for qualcomm usb devices\n");
            return ret;
        }
        if (ret == ERROR_SUCCESS_REBOOT_REQUIRED)
        {
            printf("INFO: reboot required to complete driver installation\n");
            ret = ERROR_SUCCESS;
        }
        if (ret != ERROR_SUCCESS)
        {
            printf("ERROR: failed to install driver (0x%lX)\n", ret);
            return ret;
        }

        if (!opts.installationPath.empty())
        {
            InstallationInfo info;
            info.publisher       = QCOM_USB_DRIVERS_COMPANY_NAME_W;
            info.displayName     = QCOM_USB_DRIVERS_PRODUCT_NAME_W;
            info.displayVersion  = QCOM_USB_DRIVERS_PRODUCT_VERSION_STRING_W;
            info.installLocation = opts.installationPath;
            info.uninstallString = L"\"" + opts.installationPath + L"\\qdinstall.exe\" -x";
            info.estimatedSizeKB = 0;

            printf("\nRegistering installation ...\n");
            printf("Location: %ws\n", opts.installationPath.c_str());
            ret = register_installation(info);
            if (ret != ERROR_SUCCESS)
            {
                printf("ERROR: failed to write uninstall info (0x%lX)\n", ret);
                return ret;
            }
        }
    }
    else if (opts.uninstall)
    {
        ret = uninstall_drivers();
        if (ret != ERROR_SUCCESS && ret != ERROR_FILE_NOT_FOUND)
        {
            printf("ERROR: failed to uninstall driver (0x%lX)\n", ret);
            return ret;
        }
        printf("\nCleaning up registry entries ...\n");
        ret = unregister_installation();
        if (ret != ERROR_SUCCESS)
        {
            printf("WARNING: failed to clean up registry (0x%lX), continuing...\n", ret);
        }
    }

    return ERROR_SUCCESS;
}
