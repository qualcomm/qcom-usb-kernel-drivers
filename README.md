# qcom-kernel-drivers
Qualcomm kernel drivers provide logical representations of Qualcomm chipset-enabled mobile devices over USB connections. This repository includes source code, build scripts, and documentation for a set of device drivers designed for Qualcomm hardware platforms. The drivers support both Windows and Linux environments.
The project is organized to facilitate easy compilation, testing, and integration into custom hardware solutions.

## Key Features
  - Supports Both Windows and Linux platforms.
  - Supports Windows on Snapdragon hosts and both X64/X86/ARM64 devices.
  - WHQL-certified and optimized for the latest Windows 11 operating systems.
  - Compatible with existing tools like QUTS, QDL, Qualcomm Device Launcher, and more.
    
## Repository Structure

```
/
├─ docs/                  # Architecture diagrams and design documents
├─ src/                   # Qualcomm USB kernel driver for windows and linux platform
├─ examples/              # samples scripts
├─ README.md              # This file
└─ ...                    # Other files and directories
```

## Software install guide

### Prerequisites

#### Windows

- Visual Studio 2019 (or later) with **Desktop development with C++** workload.
- Windows Driver Kit for Windows 10, version 1809 or later
- Administrator rights for driver installation.
- Perl v5.26.3 or later

#### Linux

- GNU Make, GCC/Clang.
- Kernel headers for the target kernel version (`linux-headers-$(uname -r)`).
  
### Building the Drivers

#### Windows
Clone the repository
    
```bash
git clone https://github.com/microsoft/vcpkg.git
```
Navigate to directory where the code was cloned `src\windows`

From the project root, open the .sln file with Visual Studio

In Visual Studio, select `Build` > `Build Solution` from the top menu.

Compiled binaries will be located in a path based on build configuration, for example:
```bash    
<ProjectRootDir>\x64\Debug\
<ProjectRootDir>\x64\Release\
```
      
#### Linux
```bash
cd src/linux
make
```

#### Windows command:
- Installation
```bash
<ProjectRoot>\install\install.bat
```
- Uninstallation

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

Please review the [SECURITY.md](./.github/SECURITY.md) before reporting vulnerabilities with the project

## Contributor's License Agreement

Please review the Qualcomm product [license](./LICENSE), [code of conduct](./CODE-OF-CONDUCT.md) & terms
and conditions before contributing.

## Contact

For questions, bug reports, or feature requests, please open an issue on GitHub or contact the maintainers
