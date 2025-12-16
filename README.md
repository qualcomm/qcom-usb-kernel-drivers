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
├─ examples/              # samples scripts
├─ README.md              # This file
└─ ...                    # Other files and directories
```

## Build Instructions

### Prerequisites

#### Windows

- Visual Studio 2019 (or later) with **Desktop development with C++** workload.
- Windows Driver Kit for Windows 10, version 1903 (18362.1) or later.

#### Linux

- GNU Make, GCC/Clang.
- Kernel headers for the target kernel version (`linux-headers-$(uname -r)`).
  
### Build Steps

#### Windows
1. Clone the repository
   ```bash
   git clone https://github.com/qualcomm/qcom-usb-kernel-drivers.git
   ```
2. Navigate to directory where the code was cloned
   ```bash
   cd /src/windows/<project-name>
   ```
3. From the project root, open the .vcxproj file in Visual Studio.

4. In Visual Studio, select `Build` > `Build Solution` from the top menu.

The output binaries are generated in a path depends on the chosen build configuration. For example:

    <ProjectRootDir>\x64\Debug\
    <ProjectRootDir>\x64\Release\
      
#### Linux
```bash
cd src/linux
make
```

## Install / Uninstall

#### Windows
- Installation

  Right click the `.inf` file in output folder and select **Install**.
  Or install via `pnputil`:
```bash
pnputil /add-driver <build_path/driver_name.inf> /install
```
- Uninstallation (Device Manager)
1. Open **Device Manager**.
2. Right click the target device and select **Uninstall device**.
3. Check **Attempt to remove the driver for this device**.
4. Click **Uninstall**.

- Uninstallation (Command Line)

1. Locate the **Published Name** of the installed driver package:
  ```bash
  pnputil /enum-drivers
  ```
2. Delete the driver from system
  ```bash
  pnputil /delete-driver oemxx.inf /uninstall /force
  ```
#### Linux command:
  Navigate to folder `src/linux`
    
- Installation
```bash
./driverLoad.sh install
```
- Uninstallation
```bash
./driverLoad.sh uninstall
```

## Contributing

1. Fork the repository.
2. Create a feature branch (`git checkout -b feature/my-feature`).
3. Make your changes and ensure they compile on all supported platforms.
4. Submit a pull request with a clear description of the changes.

Please follow the existing coding style and run the appropriate static analysis tools before submitting.

## Bug & Vulnerability reporting

Please review the [security](./SECURITY.md) before reporting vulnerabilities with the project

## Contributor's License Agreement

Please review the Qualcomm product [license](./LICENSE.txt), [code of conduct](./CODE-OF-CONDUCT.md) & terms
and conditions before contributing.

## Contact

For questions, bug reports, or feature requests, please open an issue on GitHub or contact the maintainers
