# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause

# Release Notes

All notable changes to the **Qualcomm USB Kernel Drivers (QUD)** project are documented in this file.

## Component Description

Qualcomm USB host drivers providing logical representations of Qualcomm chipset-enabled mobile devices over USB connection (Only for Qualcomm users).

- **Open source repository:** https://github.com/qualcomm/qcom-usb-kernel-drivers.git
- **Public QUD Wiki Reference:** https://qwiki.qualcomm.com/public/Qct-linux-usb-host-drivers
- **Contacts:** `<host-drivers.team@qti.qualcomm.com>` — For any question or comment regarding QUD driver.
- **Contributing:** If you are interested in contributing to the open-source Qualcomm USB Kernel Driver, please refer to [CONTRIBUTING.md](../../CONTRIBUTING.md), or raise an issue on GitHub for any query.

## Tools

`QMIConnectionManager` App (A tool facilities QMI calls via the Rmnet driver):

- For older releases (below 1.0.6.0): `/opt/QTI/QUD/tools/rmnet/rmnet_QMICall`
- For newer releases (1.0.6.0 and above): `/opt/qcom/QUD/tools/rmnet/rmnet_QMICall`

---

## [1.0.6.0] - 2026-03-10

1. Major release after the open-source migration.
2. Enhanced the makefile scripts for open-source users to build, install, and uninstall individual or multiple driver modules without relying on `qcom_drivers.sh`. This enables users to modify, test and debug modules independently, providing a cleaner and faster workflow for development and validation.
3. Fixed the post-installation QUDService script creation failure.
4. Support newer kernel version `6.6.14.0-63.fc42.x86_64` for Fedora FC42.
5. Enable support of CentOS 9 platform.

## [1.0.5.6]

1. Support newer kernel version 6.14 for Ubuntu and Fedora platform.

## [1.0.5.5]

1. Resolved use-after-free issue in GobiNet (RMNET) driver during disconnect event. Fix synchronization `ReleaseClient` function, which triggered a race condition in `NotifyAndPopNotifyList`.

## [1.0.5.4]

1. Resolved an issue in QUDService to ensure the QUD driver installs properly when the kernel version changes after a system reboot.
2. Addressed an installation hung issue on Ubuntu 24 while decompressing dependent kernel modules.

## [1.0.5.0]

1. Fixed page allocation failure issue due to large physical contiguous memory of 2^10 order.

## [1.1.35.3] (BETA)

1. Support for Ubuntu 24.
2. Fix race condition scenario between probe and disconnect event.
3. Fix stop read urb during disconnect event.

## [1.1.35.2] (BETA)

1. Fix a rare issue where "submitting an URB while active" could cause a driver crash.

## [1.1.35.1] (BETA)

1. Fix keyutils installation issue.
2. Fix wrong device node exposed by `qcom_usb` driver in crash mode.

## [1.1.35.0] (BETA)

1. The `qcom_usb` driver is now fully compliant with kernel open-source standards.
2. Device probing speed for both `qcom_usb` and `qcom_usbnet` drivers has been significantly improved by removing Infparser module dependencies, reducing kernel memory usage by 16,384 bytes and ensuring cleaner kernel logs.
3. Resolved a rare driver crash caused by internal memory fragmentation.
4. Optimized the `qcom_usb` driver, reducing the kernel memory footprint by 61,440 bytes.
5. Improved power management by addressing the runtime PM usage undercount issue.
6. Improved efficiency by implementing kernel `list_head` for the notify list instead of maintaining a custom list.
7. Simplified the driver installation process, allowing users to modify and test individual drivers faster and more efficiently without relying on the `qcom_drivers.sh` installer script. Detailed steps are provided in the README.md located at `/opt/qcom/QUD/`.
8. Minimized spin lock usage, enhancing overall concurrency and system responsiveness in both the `qcom_usb` and `qcom_usbnet` drivers.
9. Fixed an issue to prevent I/O operations from being triggered during disconnect events.

**Note:**
- The `QdssDiag` driver is now renamed to `qcom_usb`.
- The `GobiNet` driver is now renamed to `qcom_usbnet`.
- The `qtiDevInf` module is now deprecated and no longer required.

## [1.0.4.39]

1. Added incompatibility QUD userspace component in installer.

## [1.0.4.35]

1. Support for Ubuntu 24.
2. DRYRUNCR-3293: Fix keyutils installation issue.

## [1.0.4.34]

1. Fixed compilation issues for kernel subversion `5.15.x.xxx`.

## [1.0.4.32]

1. Major improvements in GobiNet driver. Addressing crash issues and optimizing spin lock timing to enhance system responsiveness.
2. New PID Support `913C`, `912C` (IPC Router).

## [1.0.4.31]

1. Disabled v1.0.4.30 due to QPM backend issues. Releasing new v1.0.4.31.

## [1.0.4.30]

1. Publish QUD for arm64 platform.
2. Fix diag issues.
3. Added fedora utility command.

## [1.0.4.29]

1. Fix auto-reload of blacklisted `qcserial` and `qmi_wwan` driver.
2. Fix read/write routine being triggered after a disconnect event.
3. Added Fedora support.
4. Fixed Mac UTM issues.
5. Fixed compilation issues for ubuntu kernel `5.15.0-119` version.

## [1.0.4.28]

1. Fixed QMIConnectionManager glibc issue.
2. Resolved issues when pcat build download progress halts at 25%.

## [1.0.4.27]

1. Fix compilation of drivers for kernel version 5.15.149 and above.

## [1.0.4.26]

1. Added support to kernel version 6.8.

## [1.0.4.25]

1. Fixed rmnet driver crash issue during disconnect event.
2. Resolved gcc version mismatch issue on new ubuntu machine.

## [1.0.4.24]

1. Fixes of GobiNet driver for new ubuntu patch `5.15.0-94-generic`.

## [1.0.4.23]

1. Provided support for kernel 6.5 version.
2. Fixes for QMI Mux logging issue.

## [1.0.4.22]

1. Bug fixes in QdssDiag driver.
2. Resolved keyutils missing package issue and enhanced sign readme document.

## [1.0.4.21]

1. Implemented QUDService for installing the QUD driver in case of a kernel upgrade or change.
2. Implemented the ability to compile and install QUD driver from the `/opt/QTI/QUD` directory.

## [1.0.4.20]

1. Expanded QMIConnectionManager App compatibility to Ubuntu 22.
2. Enhanced QdssDiag driver by incorporating modem interface support.
3. Fixed minor issues for kernel version 5.15.
4. Support newer kernel version 6.2.

## [1.0.4.19]

1. QUD-857, QUD-878 and QUD-882: Public Signkey generation Issue Resolved due to awk version compatibility.
2. QUD-821: QMIConnectionManager Executable: A tool facilitating QMI calls via the Rmnet driver. FDI, please refer readme and release notes located at `/opt/QTI/QUD/tools/rmnet/rmnet_QMICall`.
3. QUD-859, QUD-870, and QUD-833: Enhanced Installation and Release Processes.
4. QUD-852: Resolved "Open Denied" Refcount Issue in QdssDiag Driver.
5. Debug Message Priorities Updated.
6. Public Qwiki Updated: For the latest information, please visit https://qwiki.qualcomm.com/public/Qct-linux-usb-host-drivers.

## [1.0.4.18]

1. QUD-739, QUD-817: The udev rule conflict resulted in the loss of machine network connectivity (Google).
2. QUD-844: The Linux host became unresponsive due to a crash in the QDSS driver.
3. QUD-851: A minor issue related to qpm was addressed to prevent it from displaying drivers as installed when the script fails.
4. QUD-829: The Linux QUD drivers experienced abrupt halts.
5. QUD-732: The UDP/TCP throughput in the rmnet driver was significantly improved.
6. QUD-782: Added persistent logging support for the RMNET driver.
7. QUD-742: Enhanced the logic for handling missing sequence numbers in the rmnet driver.

## [1.0.4.17]

1. QUD-779: Fixed rmnet gobinet kthread exit issue during disconnect events.
2. QUD-756, 755, 795: Level logging that enables users to disable global logging and dynamically control logging levels.
3. QUD-813: Loading MBN items through pcat. Fixed Async calls in rmnet driver.
4. QUD-785: Support Ubuntu 22.04 (kernel version - `5.15.0-71`).

## [1.0.4.16]

1. QUD-733, QUD-748: Resolved various memory leaks problems that were leading to page allocation failure for the DIAG driver.
2. QUD-746: Resolved various memory leaks problems in RMNET driver.
3. QUD-715: Wrong IP assignment to mux adapter in rmnet driver.
4. QUD-753: Added IP down feature in rmnet driver.
5. QUD-698: Implemented ReadSyncTimeout feature to address lsusb hang problem.
6. QUD-776: We updated build script to install build-essential packages to avoid unnecessary build issue on fresh machine.
7. QUD-726: The third-party tool dependency was eliminated for QUD packaging to support all Linux distributions.
8. QUD-770: Resolved ScriptRunError while upgrading QUD in Linux PC.
9. QUD-780: Fixed RMNET dev nodes creation issue in multiple devices scenario.
10. QUD-552: Fixed error message prints in QDSS and RMNET driver.
11. STARDUST-5380: Fixed all security concerns raised by stardust team.

## [1.0.4.15]

- Minor bug in QUD external.

## [1.0.4.14]

- Enable Persistent logging which ensures the debug value remains set after reboot of device.
- Enable QUD support for redhat platform. Tested on kernel version `4.18.0-372`.
- Fixes for signing the kernel module if secure boot enabled.
- Critical race condition fixes in Qdss diag write operation.

## [1.0.3.22]

- Disabling qcserial installation while machine is up.

## [1.0.3.21]

- Installation fixes.

## [1.0.3.20]

- Muting rmnet release due to stability issues and fix for oldfs issue.

## [1.0.3.19]

- Implemented buffer based memory management for rmnet drivers.

## [1.0.3.18]

- Linked list changes in rmnet drivers and enabling rmnet in release.

## [1.0.3.17]

- Added logging mechanism based on interfaces.

## [1.0.3.16]

- Handled error code check during re-submit.

## [1.0.3.15]

- Handling watchDog issue in StopRead.

## [1.0.3.14]

- Increased TX timeout value and handled for retry code.

## [1.0.3.13]

- Lock up issue fixes.

## [1.0.3.12]

- Major bug fixes.

## [1.0.3.11]

- Sync operation implementation.

## [1.0.3.10]

- Disabling RMNET driver.
- Linked list rework.

## [1.0.3.9]

- Enabling RMNET driver and minor bug fixes.

## [1.0.3.8]

- Major rework on ring-buffer implementation and minor stability fixes.

## [1.0.3.7]

- Staging build release with PR#164.
- Rework of ring-buffer for QdssDiag driver.
- Staging release with successful crashdump collection and software download.

## [1.0.3.6]

- Staging build release with PR#164.
- Rework of ring-buffer for QdssDiag driver.
- Staging release with successful crashdump collection.

## [1.0.3.5]

- Staging build release with PR#164.
- Rework of ring-buffer for QdssDiag driver.

## [1.0.3.4]

- Bug fixes.

## [1.0.3.2]

- Bug fixes and change in method of version number update.

## [1.0.3.0]

- Bug fixes, version number updates and Copyright details update.

## [1.0.2.0]

- Multiple bug fixes.

## [1.0.1.0]

- New release component on QPM Linux. Update installer package for clean up of previous installations.

## [1.0.0.27]

- Update installer package to take care of root access while installation.

## [1.0.0.26]

- Open Source Naming convention changes and bug fixes.

## [1.0.0.25]

- Added uninstallation feature in installer.

## [1.0.0.24]

- Added new device compositions for available devices.

## [1.0.0.23]

- Fixed bugs in installer and Kernel version issues.

## [1.0.0.22]

- Updated Kona USB composition and also replaced linux conf files with win7 conf.

## [1.0.0.21]

- QUD qik package directly inserts drivers modules into kernel.

## [1.0.0.20]

- Installer file format issues.

## [1.0.0.19]

- Installer Permission issues.

## [1.0.0.18]

1. Adding qceserial to blacklist.conf.
2. First Production release.

## [1.0.0.17]

1. `copy_to_user` not supposed to be used after acquiring spinlock.

## [1.0.0.16]

1. Resource Temporarily un-available.
2. Spin Lock Crash.
3. Adding qcserial to blacklisting files.
