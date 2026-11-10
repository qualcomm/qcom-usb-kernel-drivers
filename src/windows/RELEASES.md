<!--
Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause
-->

# Release Notes

All notable changes to the **Qualcomm USB Kernel Drivers (QUD)** Windows drivers and installer are documented in this file.

## Component Description

Qualcomm USB host drivers providing logical representations of Qualcomm chipset-enabled mobile devices over USB connection (Only for Qualcomm users).

- **Open source repository:** https://github.com/qualcomm/qcom-usb-kernel-drivers.git
- **Contributing:** If you are interested in contributing to the open-source Qualcomm USB Kernel Driver, please refer to [CONTRIBUTING.md](../../CONTRIBUTING.md), or raise an issue on GitHub for any query.
- **Public QUD Wiki Reference:** https://qwiki.qualcomm.com/public/Qct-linux-usb-host-drivers
- **Contacts:** `<host-drivers.team@qti.qualcomm.com>` — For any question or comment regarding QUD driver.

---

## [1.00.95.6] - 2026-10-29
1. Completely removed 32-bit build configuration from project and inf files.
2. Added PID support for 0x9149.
3. Fixed status handling typo for qcusbwwan device configuration in the NDIS driver.
