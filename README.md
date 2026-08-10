# Qualcomm USB Kernel Drivers

Qualcomm kernel drivers provide logical representations of Qualcomm chipset-enabled mobile devices over USB connections. This repository includes source code, build scripts, and documentation for a set of device drivers designed for Qualcomm hardware platforms. The drivers support both Windows and Linux environments.
The project is organized to facilitate easy compilation, testing, and integration into custom hardware solutions.

## Key Features

- Supports Windows and Linux platforms.
- Supports X64/X86/ARM64 architectures.
- WHQL-certified on the latest Windows operating systems.
- Compatible with Qualcomm tools like QUTS, QXDM, PCAT, and more.
- Compatible with terminal emulators like PuTTY and Tera Term.

## Repository Structure

```
/
├─ docs/                  # Architecture diagrams and design documents
├─ src/                   # Qualcomm USB kernel driver for windows and linux platform
├─ examples/              # Sample scripts
├─ README.md              # This file
└─ ...                    # Other files and directories
```

---

# Windows

## Prerequisites

- Visual Studio 2019 (or later) with **Desktop development with C++** workload.
- Windows Driver Kit for Windows 10, version 1903 (18362.1) or later.
- **.NET Framework 4.7.1 Developer Pack** (required to compile the SDCM installer). Although .NET Framework 4.x is built into Windows 10/11, the Developer Pack provides the compiler reference assemblies (`csc.exe` targeting pack) that the build script needs and is **not** included in Windows by default. Download it from [Microsoft](https://dotnet.microsoft.com/en-us/download/dotnet-framework/net471) and install it manually if the build fails with a `csc.exe not found` or assembly-reference error.
  > **Note:** This is only required on Windows 11 24H2 / 25H2 and later where the targeting pack is no longer pre-installed.

## Build

1. Clone the repository:
   ```bash
   git clone https://github.com/qualcomm/qcom-usb-kernel-drivers.git
   ```
2. Open a Command Prompt and navigate to the `build` directory:
   ```bash
   cd build
   ```
3. Run the build script with the `--no_sign_required` flag:
   ```bash
   build_drivers.bat --no_sign_required
   ```

This will build the drivers and tools for all supported architectures (x86, x64, arm64) and produce a self-contained installer executable for each:

```
build\target\qcom_usb_kernel_drivers_x86.exe
build\target\qcom_usb_kernel_drivers_x64.exe
build\target\qcom_usb_kernel_drivers_arm64.exe
```

> **Note:** The `--no_sign_required` flag skips the Microsoft attestation signature check on the driver catalog files, which is useful for local testing without a signing certificate.

## Install / Uninstall

Use the installer executable generated for your target architecture. The installer must be run from an **elevated (Administrator) Command Prompt**.

### Installation

```bash
qcom_usb_kernel_drivers_<arch>.exe --install
```

For example, to install on a 64-bit system:
```bash
qcom_usb_kernel_drivers_x64.exe --install
```

### Uninstallation

```bash
qcom_usb_kernel_drivers_<arch>.exe --uninstall
```

For example:
```bash
qcom_usb_kernel_drivers_x64.exe --uninstall
```

> **Note:** Installation logs are saved to `%ProgramData%\Qualcomm\QUD\` for troubleshooting.

---

# Linux

## Prerequisites

- GNU Make, GCC/Clang.
- Kernel headers for the target kernel version (`linux-headers-$(uname -r)`).

## Build

```bash
cd src/linux
make
```

## Install / Uninstall

Navigate to the `src/linux` folder.

### Installation

```bash
sudo ./qcom_drivers.sh install
```

### Uninstallation

```bash
sudo ./qcom_drivers.sh uninstall
```

For more guidance on the build process, FAQs, and troubleshooting, please refer to the [Linux README](./src/linux/README.md).

---

## Debian Package

A Debian (`.deb`) package bundles the entire driver source tree and automatically builds and loads the kernel modules during installation via `qcom_drivers.sh`. The package name is **`qud`** and all files are installed under `/opt/qcom/QUD/build/`.

### Prerequisites

The following packages are required on the build host:

```bash
sudo apt-get install -y dpkg-dev build-essential
```

> **Note:** The build script (`build-deb.sh`) must be run on a Debian/Ubuntu host that has `dpkg-deb` available.

### Build the Debian Package

1. Clone the repository (if not already done):
   ```bash
   git clone https://github.com/qualcomm/qcom-usb-kernel-drivers.git
   cd qcom-usb-kernel-drivers
   ```

2. Navigate to the Linux source directory:
   ```bash
   cd src/linux
   ```

3. Run the build script:
   ```bash
   ./build-deb.sh
   ```

   The package is generated at:
   ```
   src/linux/build/qud_<version>_all.deb
   ```
   For example, with the current driver version `1.0.6.5`:
   ```
   src/linux/build/qud_1.0.6.5_all.deb
   ```

4. *(Optional)* To also produce a `.zip` bundle containing the `.deb`, `README.md`, and `RELEASES.md`:
   ```bash
   ./build-deb.sh zip
   ```
   Output:
   ```
   src/linux/build/qud_<version>_all.zip
   ```

#### Customization via Environment Variables

You can override default build settings by exporting environment variables before running the script:

| Variable | Default | Description |
|---|---|---|
| `PKG_NAME` | `qud` | Debian package name |
| `VERSION` | Parsed from `version.h` | Package version string |
| `ARCH` | `all` | Target architecture (`all`, `amd64`, `arm64`, `i386`) |
| `MAINTAINER` | `host-drivers.team <host-drivers.team@qti.qualcomm.com>` | Package maintainer field |
| `INSTALL_PREFIX` | `/opt/qcom/QUD/build` | Installation path inside the target system |
| `OUTPUT_DIR` | `./build` | Directory where the `.deb` file is written |
| `NO_CLEANUP` | `0` | Set to `1` to keep the temporary build working directory for inspection |

Example — build for `amd64` with a custom output directory:
```bash
ARCH=amd64 OUTPUT_DIR=/tmp/qud-out ./build-deb.sh
```

#### Inspect the Package Payload (Before Installing)

List all files that will be installed by the package:
```bash
dpkg-deb -c src/linux/build/qud_<version>_all.deb
```

---

### Install the Debian Package

```bash
sudo dpkg -i src/linux/build/qud_<version>_all.deb
```

For example:
```bash
sudo dpkg -i src/linux/build/qud_1.0.6.5_all.deb
```

During installation, `dpkg` automatically:
1. Runs the **`preinst`** script — cleans up any previous QUD installation (qpm-cli packages, legacy services, old install directory).
2. Unpacks the driver source tree to `/opt/qcom/QUD/build/`.
3. Runs the **`postinst`** script — installs kernel headers if missing, then calls `qcom_drivers.sh install` to compile and load the kernel modules.

Installation logs are written to:
```
/opt/qcom/QUD/qcom_kernel_install.log
```

> **Note:** If `dpkg` reports missing dependencies, resolve them first with:
> ```bash
> sudo apt-get install -f
> ```
> Then re-run the `dpkg -i` command.

---

### Uninstall the Debian Package

```bash
sudo dpkg -r qud
```

This runs `qcom_drivers.sh uninstall` to unload and remove the kernel modules before removing the package files. Uninstallation logs are written to:
```
/opt/qcom/QUD/qcom_kernel_uninstall.log
```

To also remove any residual configuration files (full purge):
```bash
sudo dpkg --purge qud
```

---

### Verify the Debian Package Installation

#### 1. Check the Installed Package Version

```bash
dpkg -s qud | grep -i ^Version
```

Expected output (example):
```
Version: 1.0.6.5
```

To see the full package status and metadata:
```bash
dpkg -s qud
```

Expected output (example):
```
Package: qud
Status: install ok installed
Priority: optional
Section: kernel
Installed-Size: ...
Maintainer: host-drivers.team <host-drivers.team@qti.qualcomm.com>
Architecture: all
Version: 1.0.6.5
Depends: bash, coreutils, sed, grep, make, kmod, build-essential, ...
Description: Qualcomm USB kernel drivers for QUD devices.
```

#### 2. Confirm the Package Appears in the Installed Package List

```bash
dpkg -l | grep qud
```

Expected output:
```
ii  qud   1.0.6.5   all   Qualcomm USB kernel drivers for QUD devices.
```

The `ii` prefix means the package is correctly installed.

#### 3. Verify Kernel Modules Are Loaded

```bash
lsmod | grep qcom
```

Expected output (modules currently loaded):
```
qcom_usbnet    ...
qcom_usb       ...
```

#### 4. Verify Kernel Modules Are Present on Disk

```bash
ls /lib/modules/$(uname -r)/kernel/drivers/net/usb/ | grep qcom
```

#### 5. Check the Installation Log

Review the full installation log for any warnings or errors:
```bash
cat /opt/qcom/QUD/qcom_kernel_install.log
```

#### 6. Verify Installed Files

List all files installed by the package:
```bash
dpkg -L qud
```

Confirm the driver source tree is present under the install prefix:
```bash
ls /opt/qcom/QUD/build/
```

---

### Version Number

The package version is automatically read from `src/linux/version.h` at build time:

```c
#define DRIVER_VERSION "1.0.6.5"
```

The resulting `.deb` filename encodes the version:
```
qud_<DRIVER_VERSION>_<ARCH>.deb
```

To check the version of the **currently installed** package at any time:
```bash
dpkg -s qud | grep -i ^Version
```

To check the version **embedded inside a `.deb` file** before installing it:
```bash
dpkg-deb -f src/linux/build/qud_<version>_all.deb Version
```

For a full history of version changes and release notes, see [RELEASES.md](./src/linux/RELEASES.md).

---

## Contributing

1. Fork the repository.
2. Create a feature branch (`git checkout -b feature/my-feature`).
3. Make your changes and ensure they compile on all supported platforms.
4. Submit a pull request with a clear description of the changes.

Please follow the existing coding style and run the appropriate static analysis tools before submitting.

## Bug & Vulnerability Reporting

Please review the [security policy](./SECURITY.md) before reporting vulnerabilities with the project.

## Contributor's License Agreement

Please review the Qualcomm product [license](./LICENSE.txt), [code of conduct](./CODE-OF-CONDUCT.md) & terms
and conditions before contributing.

## Contact

For questions, bug reports, or feature requests, please open an issue on GitHub or contact the maintainers.
