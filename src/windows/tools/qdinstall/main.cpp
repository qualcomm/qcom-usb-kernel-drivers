// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause

#include "main.h"
#include "registry.h"
#include "../../qcversion.h"

constexpr const wchar_t *COMMAND_QDCLR    = L".\\qdclr.exe";
constexpr const wchar_t *COMMAND_WWANSVC  = L".\\tools\\qcmtusvc.exe";
#ifdef _WIN64
constexpr const wchar_t *COMMAND_PNPUTIL_MAIN = L"pnputil /add-driver \"";
#else
constexpr const wchar_t *COMMAND_PNPUTIL_MAIN = L"C:\\Windows\\Sysnative\\pnputil.exe /add-driver \"";
#endif
constexpr const wchar_t *COMMAND_PNPUTIL_OPTS = L"\" /subdirs /install /force";

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

DWORD install_drivers(const std::wstring &path)
{
    std::wstring cmd = COMMAND_PNPUTIL_MAIN;
    cmd += path;
    cmd += L"\\*.inf";
    cmd += COMMAND_PNPUTIL_OPTS;

    printf("\nInstalling .inf from %ws ...\n", path.c_str());
    DWORD ret = execute_command(cmd);
    if (ret != ERROR_SUCCESS && ret != ERROR_SUCCESS_REBOOT_REQUIRED)
    {
        printf("ERROR: pnputil failed (exit code 0x%lX)\n", ret);
        return ret;
    }

    scan_for_hardware_changes();
    return ERROR_SUCCESS;
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
    WCHAR path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    PathRemoveFileSpecW(path);
    return path[0] ? std::wstring(path) : std::wstring(L".");
}

static void inline print_usage()
{
    printf(
        "Usage:\n"
        "  qdinstall.exe [options]\n\n"
        "Options:\n"
        "  -i -p <path>   Install drivers from path\n"
        "  -x             Uninstall drivers\n"
        "  -v             Display version information\n"
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
        // INF root: -p path if provided, otherwise the exe's own directory.
        std::wstring inf_root = opts.installationPath.empty() ? exe_dir : opts.installationPath;
        ret = install_drivers(inf_root);
        if (ret == ERROR_ACCESS_DENIED)
        {
            printf("ERROR: failed to install driver (admin required)\n");
            return ret;
        }
        if (ret == ERROR_INVALID_NAME)
        {
            printf("ERROR: failed to install driver (invalid input path)\n");
            return ret;
        }
        if (ret == ERROR_NO_MORE_ITEMS)
        {
            printf("INFO: driver already installed for qualcomm usb devices\n");
            return ERROR_SUCCESS;
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

        // Register and start WWAN service (best effort)
        if (GetFileAttributesW(COMMAND_WWANSVC) != INVALID_FILE_ATTRIBUTES)
        {
            printf("\nRegistering WWAN service ...\n");
            execute_command(std::wstring(COMMAND_WWANSVC) + L" install");
            printf("Starting WWAN service ...\n");
            execute_command(L"net start qcmtusvc");
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
        // Stop and unregister WWAN service before driver removal
        if (GetFileAttributesW(COMMAND_WWANSVC) != INVALID_FILE_ATTRIBUTES)
        {
            printf("Stopping WWAN service ...\n");
            execute_command(L"net stop qcmtusvc");
            printf("Unregistering WWAN service ...\n");
            execute_command(std::wstring(COMMAND_WWANSVC) + L" uninstall");
        }

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
