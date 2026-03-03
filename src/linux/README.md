
## Building, Installing, and Uninstalling QUD Driver Modules (Without qcom_drivers.sh)

You can now efficiently build, install, or uninstall individual or multiple driver modules without relying on the qcom_drivers.sh script.

### Build/Install/Uninstall Both Modules (qcom_usb and qcom_usbnet)

To manage both driver modules simultaneously, use the Makefile located at: `src/linux`

### Execute the following commands:

- make build  (Recommended: do not use sudo for build)
- sudo make install
- sudo make uninstall

### Build/Debug/Install/Uninstall Individual Modules

To work with a specific driver module, navigate to the respective directory:

Then execute:

- make build
- make debug  (Enables driver debug messages*)
- sudo make install
- sudo make uninstall

 *Enable debug logs for single device. for multiple device. Navigate to specific interface folder in /sys directory and `echo 2 > Debug` 

#### Benefits

- Enables users to modify, test, and debug modules independently.
- Provides a faster and cleaner workflow for development and validation.
- Users no longer need to depend on the qcom_drivers.sh script.

#### Note

- Modifying qcom_drivers.sh is not recommended, as it is reserved for full installation and uninstallation workflows.
- For complete installations, dependency handling, conflict resolution, and thorough cleanup, we recommend using qcom_driver.sh



## FAQ's and troubleshooting for linux qualcomm USB drivers

```bash
/usr/bin/make -C /lib/modules/6.8.0-60-generic/build M=/opt/QTI/QUD/Build clean
make[1]: Entering directory '/usr/lib/modules/6.8.0-60-generic/build'
make[1]: *** No rule to make target 'clean'.  Stop.
make[1]: Leaving directory '/usr/lib/modules/6.8.0-60-generic/build'
make: *** [Makefile:25: clean] Error 2
Error: Failed to generate kernel module qtiDevInf.ko, installation abort.
EFI variables are not supported on this system
/usr/bin/make -C /lib/modules/6.8.0-60-generic/build M=/opt/QTI/QUD/Build clean
make[1]: Entering directory '/usr/lib/modules/6.8.0-60-generic/build'
make[1]: *** No rule to make target 'clean'.  Stop.
make[1]: Leaving directory '/usr/lib/modules/6.8.0-60-generic/build'
make: *** [Makefile:25: clean] Error 2
```

It may indicate that the kernel headers are not installed. Without these headers, Qualcomm USB drivers compilation will not proceed. 

### Ubuntu Platform

Please follow the steps below to ensure proper installation:

#### 1.  Install Kernel Headers

```bash
sudo apt install -y linux-headers-`uname -r`
```
Try driver compilations again. If driver compilation fails or the above command fails to install header, please execute the following command.

sudo apt install linux-headers-generic linux-tools-generic linux-tools-common

To verify successful installation. Check the symbolic link:

```bash
ls -lart /usr/lib/modules/`uname -r`/build
```
Ensure that the build folder points to the correct kernel headers source. For example: 

```bash
/usr/lib/modules/6.8.0-60-generic/build -> /usr/src/linux-headers-6.8.0-60-generic
```
If the above verification steps pass, you can skip the next step and try driver installation again.

#### 2. Alternative Installation of Kernel Headers (If Required)

If the previous step did not work as expected, you may need to create a symbolic link manually. For example:

```bash
sudo ln -sfn /usr/src/linux-headers-5.4.0-132-generic/ /usr/lib/modules/6.8.0-60-generic/build
```
This step involves linking your existing headers (for ex: linux-headers-5.4.0-132-generic) to the running kernel version (uname -r), resolving any header-related issues.

Step 3: If installation is successful, verify QUD status once again!

```bash
lsmod | grep qti 
```
(you will see GobiNet, qtiDevInf, qdssdiag). If drivers are not present. Verify “/lib/modules/`uname -r`/kernel/drivers/net/usb” location for GobiNet, qtiDevInf, qdssdiag driver.