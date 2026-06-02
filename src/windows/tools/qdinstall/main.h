// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <windows.h>
#include <cfgmgr32.h>
#include <string>
#include <shlwapi.h>

struct Options
{
    bool install = true;    // default action
    bool uninstall = false;
    bool version = false;
    bool getInstallPath = false;
    std::wstring installationPath;
};

// Trigger a hardware scan to re-enumerate the device tree.
DWORD scan_for_hardware_changes();

// Run an external process and wait for it to complete.
DWORD execute_command(const std::wstring &command);

// Install all .inf drivers under input path recursively.
DWORD install_drivers(const std::wstring &path);

// Uninstall drivers (run qdclr to clean DriverStore, then rescan).
DWORD uninstall_drivers();
