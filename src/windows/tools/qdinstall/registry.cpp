// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause

#include "registry.h"

static bool set_registry_string(HKEY hKey, const wchar_t *name, const std::wstring &value, DWORD &error_code)
{
    error_code = RegSetValueExW(hKey, name, 0, REG_SZ,
                              reinterpret_cast<const BYTE *>(value.c_str()),
                              static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));

    return (error_code == ERROR_SUCCESS);
}

static bool set_registry_dword(HKEY hKey, const wchar_t *name, DWORD value, DWORD &error_code)
{
    error_code = RegSetValueExW(hKey, name, 0, REG_DWORD,
                              reinterpret_cast<const BYTE *>(&value), sizeof(DWORD));

    return (error_code == ERROR_SUCCESS);
}

static std::wstring get_registry_string(HKEY hKey, const wchar_t *name, DWORD &error_code)
{
    DWORD type = 0;
    DWORD size = 0;

    error_code = RegQueryValueExW(hKey, name, nullptr, &type, nullptr, &size);
    if (error_code != ERROR_SUCCESS || type != REG_SZ || size == 0)
    {
        return {};
    }

    std::wstring value(size / sizeof(wchar_t), L'\0');
    error_code = RegQueryValueExW(hKey, name, nullptr, &type,
                           reinterpret_cast<BYTE *>(&value[0]), &size);
    if (error_code != ERROR_SUCCESS)
    {
        return {};
    }

    // Remove trailing null if present
    if (!value.empty() && value.back() == L'\0')
    {
        value.pop_back();
    }

    return value;
}

DWORD register_installation(const InstallationInfo &info)
{
    HKEY hKey = nullptr;
    DWORD disposition = 0;
    DWORD ret = RegCreateKeyExW(HKEY_LOCAL_MACHINE, uninstall_registry_key, 0, nullptr,
                               REG_OPTION_NON_VOLATILE, KEY_WRITE | KEY_WOW64_64KEY, nullptr, &hKey, &disposition);
    if (ret == ERROR_SUCCESS)
    {
        bool success = true;
        success = success && set_registry_string(hKey, L"Publisher", info.publisher, ret);
        success = success && set_registry_string(hKey, L"DisplayName", info.displayName, ret);
        success = success && set_registry_string(hKey, L"DisplayVersion", info.displayVersion, ret);
        success = success && set_registry_string(hKey, L"InstallLocation", info.installLocation, ret);
        success = success && set_registry_string(hKey, L"UninstallString", info.uninstallString, ret);
        success = success && set_registry_dword(hKey, L"NoModify", 1, ret);
        success = success && set_registry_dword(hKey, L"NoRepair", 1, ret);
        success = success && set_registry_dword(hKey, L"EstimatedSize", info.estimatedSizeKB, ret);

        RegCloseKey(hKey);
    }

    return ret;
}

DWORD unregister_installation()
{
    HKEY hKey = nullptr;
    DWORD ret = RegOpenKeyExW(HKEY_LOCAL_MACHINE, uninstall_registry_key, 0,
                              KEY_READ | KEY_WRITE | KEY_WOW64_64KEY, &hKey);

    if (ret == ERROR_SUCCESS)
    {
        RegDeleteTreeW(hKey, nullptr);
        RegCloseKey(hKey);
        return RegDeleteKeyExW(HKEY_LOCAL_MACHINE, uninstall_registry_key, KEY_WOW64_64KEY, 0);
    }
    
    return (ret == ERROR_FILE_NOT_FOUND) ? ERROR_SUCCESS : ret;
}

bool is_installation_registered()
{
    HKEY hKey = nullptr;
    DWORD ret = RegOpenKeyExW(HKEY_LOCAL_MACHINE, uninstall_registry_key, 0, KEY_READ | KEY_WOW64_64KEY, &hKey);

    if (ret == ERROR_SUCCESS)
    {
        RegCloseKey(hKey);
        return true;
    }

    return false;
}

std::wstring get_registered_install_location()
{
    HKEY hKey = nullptr;
    DWORD ret = RegOpenKeyExW(HKEY_LOCAL_MACHINE, uninstall_registry_key, 0, KEY_READ | KEY_WOW64_64KEY, &hKey);

    if (ret != ERROR_SUCCESS)
    {
        return {};
    }

    std::wstring value = get_registry_string(hKey, L"InstallLocation", ret);
    RegCloseKey(hKey);

    return value;
}

std::wstring get_registered_install_version()
{
    HKEY hKey = nullptr;
    DWORD ret = RegOpenKeyExW(HKEY_LOCAL_MACHINE, uninstall_registry_key, 0, KEY_READ | KEY_WOW64_64KEY, &hKey);

    if (ret != ERROR_SUCCESS)
    {
        return {};
    }

    std::wstring value = get_registry_string(hKey, L"DisplayVersion", ret);
    RegCloseKey(hKey);

    return value;
}
