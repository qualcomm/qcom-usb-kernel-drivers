#!/usr/bin/env bash
set -euo pipefail

# -----------------------------------------------------------------------------
# build-deb.sh
# Builds a .deb package that installs Qualcomm USB kernel drivers.
#
# - Packs the entire qcom-usb-kernel-drivers/src/linux folder into the package
# - All files are installed under /opt/qcom/QUD
# - Executes qcom_drivers.sh install during package installation
# - Executes qcom_drivers.sh uninstall during package removal
# - Version is read from version.h (DRIVER_VERSION)
# - Output: ./build/qud_<version>_all.deb
#
# Usage:
#   ./build-deb.sh
#
# Customization via env vars:
#   PKG_NAME       (default: qud)
#   VERSION        (default: parsed from version.h)
#   ARCH           (default: all)
#   MAINTAINER     (default: "Maintainer <maintainer@example.com>")
#   DESCRIPTION    (default: generic description)
#   INSTALL_PREFIX (default: /opt/qcom/QUD)
#   OUTPUT_DIR     (default: ./build)
#   NO_CLEANUP=1   to keep build workdir for inspection
#
# Verify payload:       dpkg-deb -c build/qud_<version>_all.deb
# Query version:        dpkg -s qud | grep -i ^Version
# Install:              sudo dpkg -i build/qud_<version>_all.deb
# Uninstall:            sudo dpkg -r qud
# -----------------------------------------------------------------------------

PKG_NAME="${PKG_NAME:-qud}"

# Resolve directories relative to this script
BASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$BASE_DIR"
OUTPUT_DIR="${OUTPUT_DIR:-$BASE_DIR/build}"

# Repo root (two levels up from src/linux)
REPO_ROOT="$(cd "$BASE_DIR/../.." && pwd)"

LEGACY_SCRIPT_SRC="${LEGACY_SCRIPT_SRC:-$REPO_ROOT/scripts/linux/legacy/installer/QcDevDriver.sh}"

RELEASES_SRC="$SRC_DIR/RELEASES.md"
if [ ! -f "$RELEASES_SRC" ]; then
  echo "ERROR: RELEASES.md not found at $RELEASES_SRC" >&2
  echo "Aborting debian package creation." >&2
  exit 1
fi

# Parse version from version.h if not explicitly provided
if [ -z "${VERSION:-}" ]; then
  if [ -f "$SRC_DIR/version.h" ]; then
    VERSION="$(grep -E '^[[:space:]]*#define[[:space:]]+DRIVER_VERSION' "$SRC_DIR/version.h" \
               | head -n1 | sed -E 's/.*"([^"]+)".*/\1/')"
  fi
fi
if [ -z "${VERSION:-}" ]; then
  echo "ERROR: Could not determine VERSION from $SRC_DIR/version.h" >&2
  exit 1
fi

ARCH="${ARCH:-all}"
MAINTAINER="${MAINTAINER:-Maintainer <maintainer@example.com>}"
DESCRIPTION="${DESCRIPTION:-Qualcomm USB kernel drivers for QUD devices. Installs kernel driver sources and helper scripts, builds and loads the drivers during installation via qcom_drivers.sh.}"
INSTALL_ROOT="${INSTALL_ROOT:-/opt/qcom/QUD}"
INSTALL_PREFIX="${INSTALL_PREFIX:-$INSTALL_ROOT/build}"

# Map ARCH to a valid Debian architecture
DEB_ARCH="$ARCH"
case "$(echo "$ARCH" | tr '[:upper:]' '[:lower:]')" in
  linux-anycpu|anycpu|any|noarch|all) DEB_ARCH="all" ;;
  x86_64|x64|amd64)                   DEB_ARCH="amd64" ;;
  aarch64|arm64)                      DEB_ARCH="arm64" ;;
  armhf)                              DEB_ARCH="armhf" ;;
  i386|x86)                           DEB_ARCH="i386" ;;
  *) ;;
esac

# Pre-flight checks
if ! command -v dpkg-deb >/dev/null 2>&1; then
  echo "ERROR: dpkg-deb not found. Please run on a Debian/Ubuntu host with dpkg installed." >&2
  exit 1
fi

if [ ! -f "$SRC_DIR/qcom_drivers.sh" ]; then
  echo "ERROR: Missing required script: $SRC_DIR/qcom_drivers.sh" >&2
  exit 1
fi
if [ ! -f "$SRC_DIR/version.h" ]; then
  echo "ERROR: Missing required file: $SRC_DIR/version.h" >&2
  exit 1
fi

# Create working build directories
WORKDIR="$(mktemp -d -t "${PKG_NAME}-build-XXXXXX")"
BUILDROOT="$WORKDIR/${PKG_NAME}_${VERSION}"
trap 'if [ "${NO_CLEANUP:-0}" -ne 1 ]; then rm -rf "$WORKDIR"; else echo "Keeping workdir: $WORKDIR"; fi' EXIT

mkdir -p "$BUILDROOT/DEBIAN"
mkdir -p "$BUILDROOT$INSTALL_PREFIX"
mkdir -p "$OUTPUT_DIR"
chmod 0755 "$BUILDROOT/DEBIAN"
chmod 0755 "$BUILDROOT$INSTALL_PREFIX"

# Pack everything located at qcom-usb-kernel-drivers/src/linux into the package build payload.
# Expected contents: InfParser Makefile qcom_drivers.sh qcom_serial qcom_usb qcom_usbnet
# README.md RELEASES.md sign version.h
# Exclude only: build/ (local output) and build-deb.sh (the packaging script itself).
# On install these are unpacked under /opt/qcom/QUD.
echo "Copying driver sources from $SRC_DIR -> $BUILDROOT$INSTALL_PREFIX"
shopt -s dotglob nullglob
for entry in "$SRC_DIR"/*; do
  name="$(basename "$entry")"
  case "$name" in
    build|build-deb.sh) continue ;;
  esac
  cp -a "$entry" "$BUILDROOT$INSTALL_PREFIX/"
done
shopt -u dotglob nullglob

# Remove any kernel build artifacts that slipped in (in case user ran `make` locally)
find "$BUILDROOT$INSTALL_PREFIX" \( \
     -name '*.o' -o -name '*.ko' -o -name '*.mod' -o -name '*.mod.c' \
     -o -name '*.cmd' -o -name 'Module.symvers' -o -name 'modules.order' \
     -o -name '.tmp_versions' \) -exec rm -rf {} + 2>/dev/null || true

# Sanity check: qcom_drivers.sh must end up in the payload
if [ ! -f "$BUILDROOT$INSTALL_PREFIX/qcom_drivers.sh" ]; then
  echo "ERROR: qcom_drivers.sh was not copied into the package payload." >&2
  echo "Expected: $BUILDROOT$INSTALL_PREFIX/qcom_drivers.sh" >&2
  echo "Aborting debian package creation." >&2
  exit 1
fi
echo "Payload top-level contents (will be installed under $INSTALL_PREFIX):"
ls -la "$BUILDROOT$INSTALL_PREFIX/"

# Include README.md in the package payload (prefer src/linux, fallback repo root)
if [ -f "$SRC_DIR/README.md" ]; then
  install -m 0644 "$SRC_DIR/README.md" "$BUILDROOT$INSTALL_PREFIX/README.md"
elif [ -f "$REPO_ROOT/README.md" ]; then
  install -m 0644 "$REPO_ROOT/README.md" "$BUILDROOT$INSTALL_PREFIX/README.md"
else
  echo "NOTE: README.md not found, skipping."
fi

install -m 0644 "$RELEASES_SRC" "$BUILDROOT$INSTALL_PREFIX/RELEASES.md"

# Bundle legacy installer helper
if [ -f "$LEGACY_SCRIPT_SRC" ]; then
  mkdir -p "$BUILDROOT$INSTALL_PREFIX/legacy/installer"
  install -m 0755 "$LEGACY_SCRIPT_SRC" "$BUILDROOT$INSTALL_PREFIX/legacy/installer/QcDevDriver.sh"
else
  echo "NOTE: Legacy installer script not found at $LEGACY_SCRIPT_SRC, skipping."
fi

# Ensure scripts are executable
chmod 0755 "$BUILDROOT$INSTALL_PREFIX/qcom_drivers.sh" || true
find "$BUILDROOT$INSTALL_PREFIX" -type f -name '*.sh' -exec chmod 0755 {} \; || true

# Create DEBIAN/control
cat > "$BUILDROOT/DEBIAN/control" <<EOF
Package: $PKG_NAME
Version: $VERSION
Section: kernel
Priority: optional
Architecture: $DEB_ARCH
Maintainer: $MAINTAINER
Depends: bash, coreutils, sed, grep, make, kmod, build-essential, gawk, python3-tk, mokutil, keyutils, linux-headers-generic
Conflicts: qualcomm-userspace-driver
Replaces: qualcomm-userspace-driver
Breaks: qualcomm-userspace-driver
Description: $DESCRIPTION
EOF
chmod 0644 "$BUILDROOT/DEBIAN/control"

# Create DEBIAN/preinst - reset log file
cat > "$BUILDROOT/DEBIAN/preinst" <<EOF
#!/usr/bin/env bash
set -e

INSTALL_ROOT="$INSTALL_ROOT"
INSTALL_PREFIX="$INSTALL_PREFIX"
LOG_FILE="\$INSTALL_ROOT/qcom_kernel_install.log"

# Guard: qualcomm-userspace-driver must be removed before installing qud.
# (The control file already declares Conflicts/Breaks; this is a belt-and-braces
# check in case the user invoked `dpkg -i --force-conflicts` or similar.)
USERSPACE_STATUS="\$(dpkg-query -W -f='\${Status}' qualcomm-userspace-driver 2>/dev/null || true)"
if [ "\$USERSPACE_STATUS" = "install ok installed" ]; then
  echo "ERROR: 'qualcomm-userspace-driver' is currently installed." >&2
  echo "       The 'qud' kernel driver package conflicts with it and cannot be installed" >&2
  echo "       alongside. Please remove it first and retry:" >&2
  echo "           sudo dpkg -r qualcomm-userspace-driver" >&2
  echo "       or (to resolve automatically):" >&2
  echo "           sudo apt-get install ./qud_${VERSION}_${DEB_ARCH}.deb" >&2
  exit 1
fi

mkdir -p "\$INSTALL_ROOT" || true

# Remove any previous extracted build folder so we start fresh
if [ -d "\$INSTALL_PREFIX" ]; then
  rm -rf "\$INSTALL_PREFIX" || true
fi
mkdir -p "\$INSTALL_PREFIX" || true

if [ -e "\$LOG_FILE" ]; then
  rm -f "\$LOG_FILE" || true
fi
touch "\$LOG_FILE" || true
chmod 0644 "\$LOG_FILE" || true

exit 0
EOF
chmod 0755 "$BUILDROOT/DEBIAN/preinst"

# Create DEBIAN/postinst - run qcom_drivers.sh install
cat > "$BUILDROOT/DEBIAN/postinst" <<EOF
#!/usr/bin/env bash
set -e

INSTALL_ROOT="$INSTALL_ROOT"
INSTALL_PREFIX="$INSTALL_PREFIX"
LOG_FILE="\$INSTALL_ROOT/qcom_kernel_install.log"

echo "[QUD] Ensuring script permissions..." | tee -a "\$LOG_FILE"
find "\$INSTALL_PREFIX" -type f -name '*.sh' -exec chmod +x {} \\; || true
chmod +x "\$INSTALL_PREFIX/qcom_drivers.sh" 2>/dev/null || true
chmod +x "\$INSTALL_PREFIX/legacy/installer/QcDevDriver.sh" 2>/dev/null || true

USERSPACE_STATUS="\$(dpkg-query -W -f='\${Status}' qualcomm-userspace-driver 2>/dev/null || true)"
if [ "\$USERSPACE_STATUS" = "install ok installed" ]; then
  echo "[QUD] qualcomm-userspace-driver is installed. Removing it before installing QUD kernel driver..." >> "\$LOG_FILE" 2>&1
  DEBIAN_FRONTEND=noninteractive dpkg --remove --force-remove-reinstreq qualcomm-userspace-driver >> "\$LOG_FILE" 2>&1 || \\
  DEBIAN_FRONTEND=noninteractive dpkg --purge  --force-remove-reinstreq qualcomm-userspace-driver >> "\$LOG_FILE" 2>&1 || true
else
  echo "[QUD] qualcomm-userspace-driver is not installed, skipping removal." >> "\$LOG_FILE" 2>&1
fi

# Uninstall any QUD driver previously installed via qpm-cli
if command -v qpm-cli >/dev/null 2>&1; then
  QUD_INTERNAL_VERSION="\$(qpm-cli --info qud.internal 2>/dev/null | grep "Installed" | awk '{printf \$4}')"
  QUD_EXTERNAL_VERSION="\$(qpm-cli --info qud 2>/dev/null | grep "Installed" | awk '{printf \$4}')"
  QUD_SLT_VERSION="\$(qpm-cli --info qud.slt 2>/dev/null | grep "Installed" | awk '{printf \$4}')"

  if [ -n "\$QUD_INTERNAL_VERSION" ] || [ -n "\$QUD_EXTERNAL_VERSION" ] || [ -n "\$QUD_SLT_VERSION" ]; then
    if [ -n "\$QUD_INTERNAL_VERSION" ]; then
      echo "[QUD] Uninstalling qud.internal (\$QUD_INTERNAL_VERSION) via qpm-cli..." >> "\$LOG_FILE" 2>&1
      qpm-cli --uninstall qud.internal --silent --force >> "\$LOG_FILE" 2>&1 || true
    fi
    if [ -n "\$QUD_EXTERNAL_VERSION" ]; then
      echo "[QUD] Uninstalling qud (\$QUD_EXTERNAL_VERSION) via qpm-cli..." >> "\$LOG_FILE" 2>&1
      qpm-cli --uninstall qud --silent --force >> "\$LOG_FILE" 2>&1 || true
    fi
    if [ -n "\$QUD_SLT_VERSION" ]; then
      echo "[QUD] Uninstalling qud.slt (\$QUD_SLT_VERSION) via qpm-cli..." >> "\$LOG_FILE" 2>&1
      qpm-cli --uninstall qud.slt --silent --force >> "\$LOG_FILE" 2>&1 || true
    fi
  else
    echo "The User hasn't installed QUD driver via qpm-cli" >> "\$LOG_FILE" 2>&1
  fi
else
  echo "[QUD] qpm-cli not available, skipping qpm-cli QUD uninstall." >> "\$LOG_FILE" 2>&1
fi

# Legacy QUD service cleanup
QC_SYSTEMD_PATH=/etc/systemd/system
if [ -f \$QC_SYSTEMD_PATH/QUDService.service ]; then
    systemctl daemon-reload >> "\$LOG_FILE" 2>&1 || true
    systemctl stop QUDService >> "\$LOG_FILE" 2>&1 || true
    systemctl disable QUDService.service >> "\$LOG_FILE" 2>&1 || true
    rm -rf \$QC_SYSTEMD_PATH/QUDService.service >> "\$LOG_FILE" 2>&1 || true
elif [ ! -f \$QC_SYSTEMD_PATH/QUDService.service ]; then
    echo "\$QC_SYSTEMD_PATH/QUDService.service unit file Doesn't exist" >> "\$LOG_FILE" 2>&1
else
    echo "Error: Failed to delete \$QC_SYSTEMD_PATH/QUDService.service" >> "\$LOG_FILE" 2>&1
fi

# New QUD service cleanup
QCOM_SYSTEMD_PATH=/etc/systemd/system
QCOM_QUDSERVICE=qcom-qud.service
if [ -f \$QCOM_SYSTEMD_PATH/\$QCOM_QUDSERVICE ]; then
    systemctl daemon-reload >> "\$LOG_FILE" 2>&1 || true
    systemctl stop \$QCOM_QUDSERVICE >> "\$LOG_FILE" 2>&1 || true
    systemctl disable \$QCOM_QUDSERVICE >> "\$LOG_FILE" 2>&1 || true
    rm -rf \$QCOM_SYSTEMD_PATH/\$QCOM_QUDSERVICE >> "\$LOG_FILE" 2>&1 || true
elif [ ! -f \$QCOM_SYSTEMD_PATH/\$QCOM_QUDSERVICE ]; then
    echo "\$QCOM_SYSTEMD_PATH/\$QCOM_QUDSERVICE unit file Doesn't exist" >> "\$LOG_FILE" 2>&1
else
    echo "Error: Failed to delete \$QCOM_SYSTEMD_PATH/\$QCOM_QUDSERVICE" >> "\$LOG_FILE" 2>&1
fi

if [ -x "\$INSTALL_PREFIX/legacy/installer/QcDevDriver.sh" ]; then
  "\$INSTALL_PREFIX/legacy/installer/QcDevDriver.sh" uninstall >> "\$LOG_FILE" 2>&1 || true
fi

# Ensure kernel headers are available for the running kernel before building modules.
KREL="\$(uname -r)"
if [ ! -d "/lib/modules/\$KREL/build" ]; then
  echo "[QUD] Kernel headers for \$KREL not found, attempting to install linux-headers-\$KREL..." >> "\$LOG_FILE" 2>&1
  apt-get install -y "linux-headers-\$KREL" >> "\$LOG_FILE" 2>&1 || true
fi

echo "[QUD] Executing qcom_drivers.sh install from \$INSTALL_PREFIX..." >> "\$LOG_FILE" 2>&1
if [ -x "\$INSTALL_PREFIX/qcom_drivers.sh" ]; then
  (cd "\$INSTALL_PREFIX" && "\$INSTALL_PREFIX/qcom_drivers.sh" install) >> "\$LOG_FILE" 2>&1 \\
    || echo "[QUD] WARNING: qcom_drivers.sh install returned non-zero exit." >> "\$LOG_FILE" 2>&1
else
  echo "[QUD] ERROR: \$INSTALL_PREFIX/qcom_drivers.sh not found or not executable." >> "\$LOG_FILE" 2>&1
fi

exit 0
EOF
chmod 0755 "$BUILDROOT/DEBIAN/postinst"

# Create DEBIAN/prerm - run qcom_drivers.sh uninstall
cat > "$BUILDROOT/DEBIAN/prerm" <<EOF
#!/usr/bin/env bash
set -e

INSTALL_ROOT="$INSTALL_ROOT"
INSTALL_PREFIX="$INSTALL_PREFIX"
LOG_FILE="\$INSTALL_ROOT/qcom_kernel_uninstall.log"

if [ -e "\$LOG_FILE" ]; then
  rm -f "\$LOG_FILE" || true
fi
touch "\$LOG_FILE" || true
chmod 0644 "\$LOG_FILE" || true

echo "#############################" >> "\$LOG_FILE" 2>&1
echo "[QUD] Running uninstall hook..." >> "\$LOG_FILE" 2>&1
if [ -x "\$INSTALL_PREFIX/qcom_drivers.sh" ]; then
  cd "\$INSTALL_PREFIX" || true
  "\$INSTALL_PREFIX/qcom_drivers.sh" uninstall >> "\$LOG_FILE" 2>&1 \\
    || echo "[QUD] WARNING: qcom_drivers.sh uninstall returned non-zero exit." >> "\$LOG_FILE" 2>&1
else
  echo "[QUD] ERROR: \$INSTALL_PREFIX/qcom_drivers.sh not found or not executable." >> "\$LOG_FILE" 2>&1
fi
exit 0
EOF
chmod 0755 "$BUILDROOT/DEBIAN/prerm"

# Create DEBIAN/postrm - final cleanup after uninstall
cat > "$BUILDROOT/DEBIAN/postrm" <<EOF
#!/usr/bin/env bash
set -e

INSTALL_ROOT="$INSTALL_ROOT"
INSTALL_PREFIX="$INSTALL_PREFIX"
LOG_FILE="\$INSTALL_ROOT/qcom_kernel_uninstall.log"

case "\$1" in
  remove|purge|upgrade|disappear)
    echo "[QUD] Cleaning up \$INSTALL_ROOT/qcom_drivers.sh and \$INSTALL_PREFIX ..." >> "\$LOG_FILE" 2>&1 || true
    rm -f  "\$INSTALL_ROOT/qcom_drivers.sh"  >> "\$LOG_FILE" 2>&1 || true
    rm -rf "\$INSTALL_PREFIX"                >> "\$LOG_FILE" 2>&1 || true
    ;;
esac

exit 0
EOF
chmod 0755 "$BUILDROOT/DEBIAN/postrm"

# Build the .deb -> qud_<version>_all.deb
OUTPUT_DEB="$OUTPUT_DIR/${PKG_NAME}_${VERSION}_${DEB_ARCH}.deb"
echo "Building package -> $OUTPUT_DEB"
if dpkg-deb --help 2>&1 | grep -q -- '--root-owner-group'; then
  dpkg-deb --build --root-owner-group "$BUILDROOT" "$OUTPUT_DEB"
else
  dpkg-deb --build "$BUILDROOT" "$OUTPUT_DEB"
fi

echo "Successfully built: $OUTPUT_DEB"
echo "Install with:   sudo dpkg -i \"$OUTPUT_DEB\""
echo "Uninstall with: sudo dpkg -r $PKG_NAME"
echo "Query version:  dpkg -s $PKG_NAME | grep -i ^Version"
