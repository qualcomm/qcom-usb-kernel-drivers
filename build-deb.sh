#!/bin/bash
# Build script for qcom-usb-drivers-dkms debian package
# Usage: ./build-deb.sh
#
# Prerequisites: sudo apt-get install debhelper dkms dpkg-dev dh-dkms
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

PKG_VERSION=$(grep '#define DRIVER_VERSION' src/linux/version.h | awk '{print $3}' | tr -d '"')
echo "Building qcom-usb-drivers-dkms version $PKG_VERSION"

# Update version in dkms.conf if needed
sed -i "s/^PACKAGE_VERSION=.*/PACKAGE_VERSION=\"${PKG_VERSION}\"/" dkms.conf

# Update version in debian/changelog if needed
CHANGELOG_VER=$(head -1 debian/changelog | grep -oP '\(\K[^)]+')
if [ "$CHANGELOG_VER" != "$PKG_VERSION" ]; then
    sed -i "1s/([^)]*)/($PKG_VERSION)/" debian/changelog
fi

# Update version in postinst and preinst if needed
sed -i "s/^PKG_VERSION=.*/PKG_VERSION=\"${PKG_VERSION}\"/" debian/qcom-usb-drivers-dkms.postinst
sed -i "s/^PKG_VERSION=.*/PKG_VERSION=\"${PKG_VERSION}\"/" debian/qcom-usb-drivers-dkms.preinst

# Update version in debian/rules if needed
sed -i "s/^PKG_VERSION := .*/PKG_VERSION := ${PKG_VERSION}/" debian/rules

# Check for required build tools
for tool in dpkg-buildpackage debhelper dkms; do
    if [ "$tool" = "debhelper" ]; then
        dpkg -l debhelper >/dev/null 2>&1 || { echo "Error: $tool not installed. Run: sudo apt-get install $tool"; exit 1; }
    elif [ "$tool" = "dkms" ]; then
        dpkg -l dkms >/dev/null 2>&1 || { echo "Error: $tool not installed. Run: sudo apt-get install $tool"; exit 1; }
    else
        command -v $tool >/dev/null 2>&1 || { echo "Error: $tool not found. Run: sudo apt-get install dpkg-dev"; exit 1; }
    fi
done

# Check for dh-dkms (provides dh_dkms helper)
dpkg -l dh-dkms >/dev/null 2>&1 || { echo "Error: dh-dkms not installed. Run: sudo apt-get install dh-dkms"; exit 1; }

# Ensure debian/rules is executable
chmod +x debian/rules

# Clean previous build artifacts
rm -f ../qcom-usb-drivers-dkms_*.deb
rm -f ../qcom-usb-drivers-dkms_*.buildinfo
rm -f ../qcom-usb-drivers-dkms_*.changes
rm -rf debian/qcom-usb-drivers-dkms
rm -rf debian/.debhelper

# Build the package (unsigned)
dpkg-buildpackage -us -uc -b

echo ""
echo "========================================"
echo "Build complete! Package files:"
ls -la ../qcom-usb-drivers-dkms_${PKG_VERSION}*.deb 2>/dev/null || echo "(deb file in parent directory)"
echo ""
echo "Install with:   sudo dpkg -i ../qcom-usb-drivers-dkms_${PKG_VERSION}_all.deb"
echo "Uninstall with:  sudo dpkg -r qcom-usb-drivers-dkms"
echo "Check status:    dpkg -s qcom-usb-drivers-dkms"
echo "========================================"