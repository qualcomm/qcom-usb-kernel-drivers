# Qualcomm USB Kernel Drivers

## Introduction
Qualcomm USB kernel drivers provide logical representations of Qualcomm chipset-enabled devices and platforms over USB connection.  

## Key Features
  - Supports both Windows and Linux platforms.
  - Supports X64/X86/ARM64 architectures.
  - WHQL-certified for the latest Windows operating systems.
  - Compatible with Qualcomm tools like QUTS, QXDM, PCAT, and more.

## Repository Structure
```
/
├── src/
│   └── usb/                        # USB driver source codes
│       ├── linux/                  # Linux kernel drivers
│       │   ├── GobiSerial/         # Serial communication driver
│       │   ├── InfParser/          # INF file parser utility
│       │   ├── QdssDiag/           # QDSS diagnostics driver
│       │   ├── rmnet/              # RmNet network driver with QMI support
│       │   └── sign/               # Driver signing configuration
│       └── windows/                # Windows kernel drivers
│           ├── filter/             # USB filter driver
│           ├── ndis/               # NDIS miniport network driver
│           ├── qdss/               # Qualcomm Debug Subsystem driver
│           ├── transport/          # USB transport layer
│           └── wdfserial/          # Serial and Diag driver
├── CODE-OF-CONDUCT.md              # Community guidelines
├── CONTRIBUTING.md                 # Contribution guidelines
├── LICENSE.txt                     # License information
├── README.md                       # Main repository documentation
└── SECURITY.md                     # Security policies and vulnerability reporting
```

## Prerequisites
### Windows
- Visual Studio 2019 (or later) with **Desktop development with C++** workload.
- Windows Driver Kit for Windows 10, version 1903 (18362.1) or later.

### Linux
- GNU Make, GCC/Clang.
- Kernel headers for the target kernel version (`linux-headers-$(uname -r)`).

## Building the Drivers
### Windows
1. Clone the repository
   ```bash
   git clone https://github.com/qualcomm/qcom-usb-kernel-drivers.git
   ```
2. Navigate to directory where the code was cloned
   ```bash
   cd /src/usb/windows/<project-name>
   ```
3. From the project root, open the .sln file in Visual Studio.

4. In Visual Studio, select `Build` > `Build Solution` from the top menu.

Compiled binaries will be located in a path based on selected build configuration, for example:

    <ProjectRootDir>\x64\Debug\
    <ProjectRootDir>\x64\Release\

### Linux
   ```bash
   cd /src/usb/linux
   make
   ```
## Installation/Uninstallation
### Windows:
  #### Installation
  Right click to install the .inf in build path. Or use pnputil in command prompt:
  
    pnputil /add-driver <build_path/driver_name.inf> /install
  #### Uninstallation
You can remove the driver using either Device Manager or the command line.

#### Option 1: Uninstall via Device Manager
1. Open **Device Manager**.
2. Right‑click the target device and select **Uninstall device**.
3. Check **Attempt to remove the driver for this device**.
4. Click **Uninstall**.

#### Option 2: Uninstall via Command Line
Locate the **Published Name** of target device from installed driver packages:
    
    pnputil /enum-drivers

Use the command to delete driver from system 
  
    pnputil /delete-driver oemxx.inf /uninstall /force

#### Linux command:
  Navigate to linux root folder
    
    cd /src/usb/linux
  #### Installation
    ./QcDevDriver.sh install
  #### Uninstallation
    ./QcDevDriver.sh uninstall

## License
Licensed under [BSD 3-Clause License](./LICENSE.txt)
