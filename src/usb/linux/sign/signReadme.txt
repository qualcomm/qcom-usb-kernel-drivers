This document provides guidance on enrolling the public key on the host machine for the successful installation of the QUD driver.
If Secure Boot is enabled in a system, all kernel modules must be signed with a private key and authenticated with the corresponding public key.

Follow the steps below to enroll the necessary public key.

Step 1 -

Enrolling public key on target system by adding the public key to the MOK list.

Execute below command on bash terminal:

sudo mokutil --import /opt/QTI/sign/Signkey_pub.der

You will be asked to enter and confirm a password for this MOK enrollment request.


Step 2 -

Reboot the machine. At boot time, choose "Enroll MOK" and enter the password that chosen in step 1.


Step 3 -

After reboot, verify the information about the public keys on system keyrings by using below command -

sudo keyctl list %:.platform

If \"hostname signing key : ***********************************\" is not present then repeat step 2.


Step 4 -

Now, public keys becomes permanent part of UEFI Secure Boot key database.

Please try again to install QUD driver from qpm server. Now signing and insertion of modules will be taken care by installer itself.
