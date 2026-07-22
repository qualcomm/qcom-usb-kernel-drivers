// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// Workaround for a bug in Windows SDK 10.0.26100.0 where winnt.h references
// _CountOneBits64, an intrinsic that is not available in the ARM64 MSVC
// compiler. Defining it as __popcnt64 (the correct ARM64 equivalent) before
// windows.h is included prevents the C3861 "identifier not found" error.
#if defined(_M_ARM64) && !defined(_CountOneBits64)
#include <intrin.h>
#define _CountOneBits64 __popcnt64
#endif

#include <windows.h>
#include <cfgmgr32.h>
#include <pathcch.h>
#include <string>

struct Options
{
    bool install = true;    // default action
    bool uninstall = false; // -u: uninstall drivers only (no directory deletion)
    bool remove = false;    // -x: uninstall drivers and remove installation files
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
