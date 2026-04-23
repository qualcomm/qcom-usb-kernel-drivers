/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                       M A I N . C P P

GENERAL DESCRIPTION
    This file implements device scanning, enumeration, and monitoring
    functions for Qualcomm USB devices on Windows.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#include "infdev.h"

int main()
{
    remove_drivers(EXEC_MODE::REMOVE_OEM);
    printf("Qualcomm USB Kernel Driver Cleanup Utility\n");
    printf("Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.\n");
}
