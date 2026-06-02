// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <windows.h>
#include <string>

constexpr const wchar_t *uninstall_registry_key =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Qualcomm USB Drivers";

struct InstallationInfo
{
    std::wstring publisher;
    std::wstring displayName;
    std::wstring displayVersion;
    std::wstring installLocation;
    std::wstring uninstallString;
    DWORD estimatedSizeKB = 0;
};

// Registration functions - return ERROR_SUCCESS (0) on success, Win32 error code on failure.
DWORD register_installation(const InstallationInfo &info);
DWORD unregister_installation();
bool is_installation_registered();
std::wstring get_registered_install_location();
std::wstring get_registered_install_version();
