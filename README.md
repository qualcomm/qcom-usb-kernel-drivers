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

---

## Debian Package

### Install

```bash
sudo dpkg -i src/linux/build/qud_<version>_all.deb
```

> **Note:** If `dpkg` reports missing dependencies, resolve them first with:
> ```bash
> sudo apt-get install -f
> ```
> Then re-run the `dpkg -i` command.

### Uninstall

```bash
sudo dpkg -r qud
```

---

### Verify Kernel Modules Are Loaded

```bash
lsmod | grep qcom
```

### Verify Kernel Modules Are Present on Disk

```bash
ls /lib/modules/$(uname -r)/kernel/drivers/net/usb/ | grep qcom_usbnet
ls /lib/modules/$(uname -r)/kernel/drivers/usb/misc/ | grep qcom_usb
```

---

## Contributing

1. Fork the repository.
2. Create a feature branch (`git checkout -b feature/my-feature`).
3. Make your changes and ensure they compile on all supported platforms.
4. Submit a pull request with a clear description of the changes.

Please follow the existing coding style and run the appropriate static analysis tools before submitting. For full contribution guidelines, see [CONTRIBUTING.md](./CONTRIBUTING.md).

## Bug & Vulnerability Reporting

Please review the [security policy](./SECURITY.md) before reporting vulnerabilities with the project.

## Contributor's License Agreement

Please review the Qualcomm product [license](./LICENSE.txt), [code of conduct](./CODE-OF-CONDUCT.md) & terms
and conditions before contributing.

## Contact

For questions, bug reports, or feature requests, please open an issue on GitHub or contact the maintainers.
