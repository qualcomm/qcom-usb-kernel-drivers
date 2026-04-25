# Qualcomm USB Kernel Drivers — Linux Installer Guide

This guide walks you through installing the Qualcomm USB kernel drivers
(`qcom-usb-drivers-dkms`) on Linux, and diagnoses the most common issues that
can arise across distributions, kernels, Secure Boot states, and immutable
systems.

It is the **human-readable companion** to the preflight tool:

```bash
sudo bash scripts/check-install-env.sh
```

The preflight tool detects your environment, cross-references it against
[`scripts/known-good-configs.json`](../../scripts/known-good-configs.json),
and classifies the system as `pass` / `partial` / `fail` / `unknown`. Every
finding in its report maps to a section in this guide.

---

## Table of contents

1. [Quick start](#quick-start)
2. [Run the preflight check first](#run-the-preflight-check-first)
3. [Supported environments at a glance](#supported-environments-at-a-glance)
4. [Step-by-step install](#step-by-step-install)
5. [Troubleshooting by finding code](#troubleshooting-by-finding-code)
6. [Distribution-specific notes](#distribution-specific-notes)
7. [Secure Boot](#secure-boot)
8. [Immutable / OSTree systems](#immutable--ostree-systems)
9. [Reporting results back](#reporting-results-back)

---

## Quick start

On Ubuntu 22.04 / 24.04 or Debian 12:

```bash
git clone https://github.com/qualcomm/qcom-usb-kernel-drivers.git
cd qcom-usb-kernel-drivers
sudo apt-get install -y dkms debhelper dpkg-dev dh-dkms "linux-headers-$(uname -r)"
sudo bash scripts/check-install-env.sh          # preflight
./build-deb.sh
sudo dpkg -i ../qcom-usb-drivers-dkms_*_all.deb
```

On other distributions, use the `Makefile` under `src/linux/` directly —
see [Distribution-specific notes](#distribution-specific-notes).

---

## Run the preflight check first

```bash
sudo bash scripts/check-install-env.sh [--json] [--report-file report.json] [--strict]
```

Exit codes:

| Code | Status   | Meaning                                                             |
|------|----------|---------------------------------------------------------------------|
| 0    | pass     | System matches a known-good configuration and all deps are present. |
| 1    | partial  | Supported but with caveats (see the warnings printed).              |
| 2    | fail     | Hard prerequisite missing or known-bad combination.                 |
| 3    | unknown  | System isn't in `known-good-configs.json` yet.                      |
| 4    | error    | The script itself failed.                                           |

Copy the JSON report into any bug report or JIRA ticket so maintainers see
your exact configuration in one step:

```bash
sudo bash scripts/check-install-env.sh --json --report-file install-report.json
```

---

## Supported environments at a glance

The authoritative source of truth is
[`scripts/known-good-configs.json`](../../scripts/known-good-configs.json).
A snapshot at the time of writing:

| Distribution  | Versions            | Arches           | Status    | Notes                                                   |
|---------------|---------------------|------------------|-----------|---------------------------------------------------------|
| Ubuntu        | 22.04, 24.04 LTS    | x86_64, arm64    | pass      | Primary development target.                             |
| Debian        | 12                  | x86_64, arm64    | pass      | Use `linux-headers-arm64` on ARM.                       |
| Debian        | 11                  | x86_64           | partial   | `dh-dkms` not available; build-deb.sh falls back.       |
| Linux Mint    | 21.x                | x86_64           | pass      | Behaves as Ubuntu 22.04.                                |
| Pop!_OS       | 22.04               | x86_64           | pass      | Signed kernel; MOK enrollment straightforward.          |
| Fedora        | 40, 41              | x86_64, aarch64  | untested  | Use the `src/linux` Makefile; `.deb` does not apply.    |
| Fedora Silverblue / Kinoite | any     | x86_64, aarch64  | untested  | Immutable rootfs; requires `rpm-ostree` layering.       |
| RHEL          | 9                   | x86_64, aarch64  | untested  | `dkms` via EPEL; use Makefile.                          |
| CentOS Stream | 9                   | x86_64, aarch64  | partial   | RMNET crash under investigation (QUD-1629).             |
| Arch Linux    | rolling             | x86_64           | untested  | Rolling release; Makefile path only.                    |

> **If your system is not in the table**, the preflight tool will mark it
> `unknown`. That is not a failure — please
> [report results back](#reporting-results-back) so we can add your
> combination.

---

## Step-by-step install

### 1. Install build dependencies

| Distro family  | Command                                                                                        |
|----------------|------------------------------------------------------------------------------------------------|
| Debian / Ubuntu| `sudo apt-get install -y dkms debhelper dpkg-dev dh-dkms "linux-headers-$(uname -r)"`          |
| Fedora         | `sudo dnf install -y dkms kernel-devel kernel-headers make gcc`                                |
| RHEL / CentOS  | `sudo dnf install -y epel-release && sudo dnf install -y dkms kernel-devel kernel-headers make gcc` |
| openSUSE       | `sudo zypper install -y dkms kernel-devel make gcc`                                            |
| Arch           | `sudo pacman -S --needed dkms linux-headers base-devel`                                        |

### 2. Run the preflight check

```bash
sudo bash scripts/check-install-env.sh
```

Address any `[FAIL]` items before proceeding. `[WARN]` items are informational
but will be surfaced again during the install itself (e.g. Secure Boot MOK
enrollment, conflicting in-tree modules).

### 3. Build and install

**Debian / Ubuntu (recommended):**

```bash
./build-deb.sh
sudo dpkg -i ../qcom-usb-drivers-dkms_*_all.deb
```

The `.deb` postinstall script will:

- Register the driver with DKMS and build modules for the running kernel.
- Blacklist conflicting in-tree modules (`qcserial`, `qmi_wwan`, `cdc_wdm`,
  `option`, `usb_wwan`) via `/etc/modprobe.d/qcom-usb-drivers-dkms.conf`.
- Install udev rules and reload `udevadm`.
- `modprobe` all four Qualcomm modules in the correct order.

**Other distributions:**

```bash
cd src/linux
make
sudo make install
```

### 4. Verify

```bash
dpkg -s qcom-usb-drivers-dkms | grep Version   # Debian/Ubuntu only
dkms status
lsmod | grep -E 'qtiDevInf|qcom_usb|qcom_usbnet|qcom-serial'
```

Plug in a Qualcomm USB device — you should see `/dev/qcqmi*` and
`/dev/ttyUSB*` entries appear.

---

## Troubleshooting by finding code

Every finding emitted by `check-install-env.sh` has a stable code. Find the
code in the table below for remediation details.

| Code                      | Level | What it means                                                      | Fix                                                                                                  |
|---------------------------|-------|--------------------------------------------------------------------|------------------------------------------------------------------------------------------------------|
| `NO_OS_RELEASE`           | WARN  | `/etc/os-release` is missing.                                      | Run on a standard Linux distro; container images sometimes strip it.                                 |
| `IMMUTABLE_ROOTFS`        | WARN  | OSTree/Silverblue/Kinoite/MicroOS detected.                        | See [Immutable / OSTree systems](#immutable--ostree-systems).                                        |
| `SECURE_BOOT_MOK`         | WARN  | Secure Boot is on but no DKMS/MOK key is enrolled.                 | See [Secure Boot](#secure-boot).                                                                     |
| `MISSING_HEADERS`         | FAIL  | Kernel headers for the running kernel are not installed.           | `sudo apt-get install -y "linux-headers-$(uname -r)"` (or distro equivalent).                        |
| `MISSING_DKMS`            | FAIL  | `dkms` is not installed.                                           | `sudo apt-get install -y dkms` (Debian/Ubuntu), `sudo dnf install -y dkms` (Fedora/EPEL).            |
| `MISSING_GCC`             | FAIL  | No C compiler found.                                               | `sudo apt-get install -y build-essential` / `sudo dnf groupinstall -y 'Development Tools'`.          |
| `MISSING_MAKE`            | FAIL  | `make` not installed.                                              | Included in the build-essential / Development Tools / base-devel meta-packages above.                |
| `MISSING_DEB_BUILD_DEPS`  | WARN  | `debhelper`, `dpkg-dev`, or `dh-dkms` missing on a Debian host.    | `sudo apt-get install -y debhelper dpkg-dev dh-dkms`.                                                |
| `CONFLICTING_MODULES`     | WARN  | In-tree modules that collide with the Qualcomm drivers are loaded. | The `.deb` postinstall handles this; if installing manually: `sudo modprobe -r <module>`.            |
| `NO_CONFIGS_FILE`         | WARN  | `known-good-configs.json` is missing from the repo.                | Re-clone the repo — this file ships with the source tree.                                            |
| `NO_PYTHON`               | WARN  | `python3` is not available for the classifier.                     | `sudo apt-get install -y python3` (or distro equivalent).                                            |
| `NO_MATCHER`              | WARN  | `scripts/match_known_good.py` is missing.                          | Re-clone the repo.                                                                                   |
| `MATCH_PASS`              | INFO  | You are on a known-good combination.                               | No action needed.                                                                                    |
| `MATCH_PARTIAL`           | WARN  | Supported but with caveats (see printed notes).                    | Follow the caveat's mitigation.                                                                      |
| `MATCH_FAIL`              | FAIL  | This exact combo is known to fail.                                 | Upgrade kernel/distro, or pick a supported entry.                                                    |
| `MATCH_UNTESTED`          | WARN  | Configuration listed but not yet validated.                        | Proceed and [report results](#reporting-results-back).                                               |
| `MATCH_UNKNOWN`           | WARN  | No matching entry in `known-good-configs.json`.                    | Proceed if `dkms`/headers are present, then [report results](#reporting-results-back).               |

---

## Distribution-specific notes

### Ubuntu / Linux Mint / Pop!_OS

- Use the `.deb` flow exactly as in [Quick start](#quick-start).
- On HWE kernels, install `linux-headers-generic-hwe-22.04` (or equivalent).
- On Pop!_OS, the kernel is System76-signed; MOK enrollment is straightforward.

### Debian 12

- Identical flow to Ubuntu, but headers are named `linux-headers-amd64` or
  `linux-headers-arm64`.

### Debian 11

- `dh-dkms` is not packaged. `build-deb.sh` detects this and falls back to
  `dh_dkms`. Newer packaging features (e.g., automatic signing integration)
  are unavailable; the drivers themselves work.

### Fedora / RHEL / CentOS Stream

- `.deb` packaging does not apply. Use the raw build path:

  ```bash
  sudo dnf install -y dkms kernel-devel kernel-headers make gcc
  cd src/linux
  make
  sudo make install
  ```

- On RHEL, enable EPEL first: `sudo dnf install -y epel-release`.
- An RPM package is a planned future contribution; track it via the
  `scripts/known-good-configs.json` entries marked `untested`.

### openSUSE Leap / Tumbleweed

- Use zypper to install build deps, then the Makefile path as for Fedora.

### Arch Linux

- Rolling release; kernel headers change often. After every kernel upgrade,
  run `sudo dkms autoinstall` (automatic if you keep `dkms.service` enabled).

---

## Secure Boot

If the preflight emits `SECURE_BOOT_MOK`:

1. On first install, DKMS prints instructions and generates a Machine Owner
   Key under `/var/lib/dkms/mok.{pub,key}`.
2. Enroll it:
   ```bash
   sudo mokutil --import /var/lib/dkms/mok.pub
   ```
   You'll be prompted for a one-time password.
3. Reboot. On the next boot, the UEFI **MOK Manager** screen will appear.
   Choose *Enroll MOK* → *Continue* → *Yes* and enter the password.
4. After boot, verify:
   ```bash
   mokutil --list-enrolled | grep -i DKMS
   lsmod | grep qtiDevInf
   ```

If Secure Boot is enabled and no MOK is enrolled, DKMS-built modules will
**silently fail to load**. Re-running `check-install-env.sh` after reboot
should now show `MOK enrolled: yes`.

---

## Immutable / OSTree systems

Fedora Silverblue, Kinoite, IoT Core, openSUSE MicroOS and similar
transactional systems keep `/usr` read-only, so the standard `dkms install`
flow cannot write into `/lib/modules`.

Two supported workarounds:

### 1. Layer the package with rpm-ostree (Silverblue / Kinoite)

```bash
sudo rpm-ostree install dkms kernel-devel akmods
# Build and install the Qualcomm drivers from src/linux into the new deployment:
cd src/linux
sudo rpm-ostree usroverlay    # opens a writable transient overlay
make && sudo make install
sudo systemctl reboot
```

Because `usroverlay` is transient, you'll need to either:

- Package the built modules into a small RPM and layer that with
  `rpm-ostree install`, **or**
- Rebuild after every OSTree update.

### 2. Use a toolbox / distrobox container

```bash
toolbox create --image registry.fedoraproject.org/fedora:40
toolbox enter
sudo dnf install -y dkms kernel-devel-$(uname -r) kernel-headers make gcc
# Then clone and build inside the container; copy .ko files out.
```

This path is only useful for *development* — the host still won't load
unsigned modules under Secure Boot without MOK enrollment.

---

## Reporting results back

The whole point of the preflight + known-good-configs architecture is that
every user can extend our coverage with a single PR.

1. Run the preflight and save the JSON:

   ```bash
   sudo bash scripts/check-install-env.sh --json --report-file install-report.json
   ```

2. Attempt the install.

3. Edit [`scripts/known-good-configs.json`](../../scripts/known-good-configs.json)
   and append an entry like:

   ```json
   {
     "distro_id": "ubuntu",
     "distro_version": "25.04",
     "kernel_range": "6.14..6.14",
     "arch": ["x86_64"],
     "status": "pass",
     "secure_boot": "manual-mok-required",
     "required_packages": ["dkms", "debhelper", "dpkg-dev", "dh-dkms", "linux-headers-generic"],
     "notes": "Clean install succeeded; 4 modules load; USB device enumerates.",
     "validated_by": "your-github-handle",
     "validated_on": "2026-05-01"
   }
   ```

4. Open a PR titled `chore(installer): add known-good entry for <distro/version>`
   and attach the `install-report.json` to the PR description.

Over time the file becomes a living compatibility matrix, and the preflight
tool's classifier gets more accurate for every user that contributes.
