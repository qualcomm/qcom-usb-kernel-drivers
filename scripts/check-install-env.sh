#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause-Clear
#
# check-install-env.sh - Preflight environment analyzer for qcom-usb-drivers-dkms.
#
# Detects distro/kernel/arch/Secure Boot/DKMS state, cross-references
# scripts/known-good-configs.json (via scripts/match_known_good.py) and
# classifies the system as pass | partial | fail | unknown with concrete
# remediation hints.
#
# Usage: sudo bash scripts/check-install-env.sh [options]
#   --json              Emit JSON report on stdout
#   --report-file PATH  Also write the JSON report to PATH
#   --configs-file PATH Use an alternate known-good-configs.json
#   --no-color          Disable ANSI color
#   --strict            Exit non-zero unless status == pass
#   -h, --help          Show this help
#
# Exit: 0=pass 1=partial 2=fail 3=unknown 4=script error

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONFIGS_FILE="${SCRIPT_DIR}/known-good-configs.json"
MATCHER="${SCRIPT_DIR}/match_known_good.py"
REPORT_FILE=""
MODE="text"
COLOR=1
STRICT=0

declare -a FINDINGS=()
STATUS="pass"
MATCH_STATUS="unknown"
MATCH_NOTES=""
MATCH_REQ=""

D_ID="" D_VER="" D_PRETTY=""
KERNEL="" KMM="" ARCH=""
SB="unknown" MOK="unknown"
HDR_OK=0 HDR_PATH=""
DKMS_VER="" GCC_VER="" MAKE_VER=""
IMMUTABLE=0 PM="unknown"
CONFLICTS=""

cc() {
    local code=$1; shift
    if [[ $COLOR -eq 1 && -t 1 ]]; then printf '\033[%sm%s\033[0m' "$code" "$*"
    else printf '%s' "$*"; fi
}
red()    { cc '1;31' "$@"; }
green()  { cc '1;32' "$@"; }
yellow() { cc '1;33' "$@"; }
blue()   { cc '1;34' "$@"; }
bold()   { cc '1'    "$@"; }

add() {
    FINDINGS+=("$1|$2|$3|${4-}")
    case "$1" in
        error) [[ "$STATUS" != "fail" ]] && STATUS="fail" ;;
        warn)  [[ "$STATUS" == "pass" ]] && STATUS="partial" ;;
    esac
}

have() { command -v "$1" >/dev/null 2>&1; }

je() {
    local s=${1-}
    s=${s//\\/\\\\}; s=${s//\"/\\\"}
    s=${s//$'\n'/\\n}; s=${s//$'\r'/\\r}; s=${s//$'\t'/\\t}
    printf '%s' "$s"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --json) MODE="json"; shift ;;
        --report-file) REPORT_FILE="$2"; shift 2 ;;
        --configs-file) CONFIGS_FILE="$2"; shift 2 ;;
        --no-color) COLOR=0; shift ;;
        --strict) STRICT=1; shift ;;
        -h|--help) sed -n '2,20p' "$0" | sed 's/^# \?//'; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 4 ;;
    esac
done

detect_distro() {
    if [[ -r /etc/os-release ]]; then
        # shellcheck disable=SC1091
        . /etc/os-release
        D_ID="${ID:-unknown}"
        D_VER="${VERSION_ID:-unknown}"
        D_PRETTY="${PRETTY_NAME:-$D_ID}"
    else
        D_ID="unknown"; D_VER="unknown"; D_PRETTY="unknown"
        add warn NO_OS_RELEASE "/etc/os-release not found" "Run on a standard Linux distro."
    fi
    if have rpm-ostree && rpm-ostree status >/dev/null 2>&1; then
        IMMUTABLE=1
        add warn IMMUTABLE_ROOTFS "Immutable (OSTree) filesystem; /usr is read-only." \
            "Use 'rpm-ostree install dkms kernel-devel' or a toolbox container."
    fi
    if   have apt-get; then PM="apt"
    elif have dnf;     then PM="dnf"
    elif have yum;     then PM="yum"
    elif have zypper;  then PM="zypper"
    elif have pacman;  then PM="pacman"
    fi
}

detect_kernel_arch() {
    KERNEL="$(uname -r)"
    ARCH="$(uname -m)"
    KMM="$(printf '%s' "$KERNEL" | awk -F. '{printf "%s.%s", $1, $2}')"
}

detect_secure_boot() {
    if have mokutil; then
        local out; out="$(mokutil --sb-state 2>/dev/null || true)"
        case "$out" in
            *"SecureBoot enabled"*)  SB="enabled" ;;
            *"SecureBoot disabled"*) SB="disabled" ;;
        esac
    elif [[ -d /sys/firmware/efi ]]; then
        local v; v="$(find /sys/firmware/efi/efivars -maxdepth 1 -name 'SecureBoot-*' 2>/dev/null | head -n1)"
        if [[ -n "$v" ]]; then
            if od -An -tu1 -j4 -N1 "$v" 2>/dev/null | grep -q '1'; then SB="enabled"
            else SB="disabled"; fi
        fi
    fi
    if [[ "$SB" == "enabled" ]]; then
        if have mokutil && mokutil --list-enrolled 2>/dev/null | grep -qi 'DKMS\|MOK'; then
            MOK="yes"
        else
            MOK="no"
            add warn SECURE_BOOT_MOK "Secure Boot enabled but no DKMS/MOK key enrolled." \
                "DKMS will prompt to create and enroll a MOK on install (mokutil --import)."
        fi
    fi
}

detect_headers() {
    local p
    for p in "/lib/modules/$KERNEL/build" "/usr/lib/modules/$KERNEL/build" \
             "/usr/src/linux-headers-$KERNEL" "/usr/src/kernels/$KERNEL"; do
        if [[ -d "$p" && -e "$p/Makefile" ]]; then
            HDR_OK=1; HDR_PATH="$p"; break
        fi
    done
    if [[ $HDR_OK -eq 0 ]]; then
        local h
        case "$PM" in
            apt)    h="sudo apt-get install -y linux-headers-$KERNEL || sudo apt-get install -y linux-headers-generic" ;;
            dnf)    h="sudo dnf install -y kernel-devel-$KERNEL kernel-headers" ;;
            yum)    h="sudo yum install -y kernel-devel-$KERNEL kernel-headers" ;;
            zypper) h="sudo zypper install -y kernel-devel=$KERNEL" ;;
            pacman) h="sudo pacman -S --needed linux-headers" ;;
            *)      h="Install kernel headers for $KERNEL via your package manager." ;;
        esac
        add error MISSING_HEADERS "Kernel headers for $KERNEL not found; DKMS cannot build." "$h"
    fi
}

detect_toolchain() {
    if have dkms; then
        DKMS_VER="$(dkms --version 2>/dev/null | head -n1 | awk '{print $NF}')"
    else
        local h
        case "$PM" in
            apt)     h="sudo apt-get install -y dkms" ;;
            dnf|yum) h="sudo dnf install -y dkms    # enable EPEL on RHEL" ;;
            zypper)  h="sudo zypper install -y dkms" ;;
            pacman)  h="sudo pacman -S --needed dkms" ;;
            *)       h="Install dkms via your package manager." ;;
        esac
        add error MISSING_DKMS "dkms not installed." "$h"
    fi
    if have gcc; then GCC_VER="$(gcc -dumpversion 2>/dev/null)"
    else add error MISSING_GCC "gcc not installed." "Install build-essential / Development Tools / base-devel."
    fi
    if have make; then MAKE_VER="$(make --version 2>/dev/null | head -n1 | awk '{print $NF}')"
    else add error MISSING_MAKE "make not installed." "Install make via your package manager."
    fi
    if [[ "$PM" == "apt" ]]; then
        local pkg missing=()
        for pkg in debhelper dpkg-dev; do
            dpkg -s "$pkg" >/dev/null 2>&1 || missing+=("$pkg")
        done
        if ! dpkg -s dh-dkms >/dev/null 2>&1; then missing+=("dh-dkms"); fi
        if [[ ${#missing[@]} -gt 0 ]]; then
            add warn MISSING_DEB_BUILD_DEPS "Missing Debian build deps: ${missing[*]}" \
                "sudo apt-get install -y ${missing[*]}"
        fi
    fi
}

detect_conflicts() {
    local m loaded=()
    for m in qcserial qmi_wwan cdc_wdm option usb_wwan; do
        if lsmod 2>/dev/null | awk '{print $1}' | grep -qx "$m"; then
            loaded+=("$m")
        fi
    done
    if [[ ${#loaded[@]} -gt 0 ]]; then
        CONFLICTS="${loaded[*]}"
        add warn CONFLICTING_MODULES "In-tree modules loaded that conflict with Qualcomm drivers: ${loaded[*]}" \
            "The .deb postinst blacklists them. Manual: sudo modprobe -r ${loaded[*]}"
    fi
}

match_known_good() {
    if [[ ! -r "$CONFIGS_FILE" ]]; then
        MATCH_STATUS="unknown"
        MATCH_NOTES="known-good-configs.json not found"
        add warn NO_CONFIGS_FILE "$MATCH_NOTES at $CONFIGS_FILE" "Skipping known-good matching."
        return
    fi
    local py=""
    if have python3; then py="python3"
    elif have python; then py="python"
    else
        MATCH_STATUS="unknown"
        MATCH_NOTES="python3 not available"
        add warn NO_PYTHON "$MATCH_NOTES" "Install python3 to enable the classifier."
        return
    fi
    if [[ ! -r "$MATCHER" ]]; then
        MATCH_STATUS="unknown"
        MATCH_NOTES="matcher script not found"
        add warn NO_MATCHER "$MATCHER missing" "Re-clone the repository to restore scripts/match_known_good.py."
        return
    fi
    local line status notes req
    line="$("$py" "$MATCHER" "$CONFIGS_FILE" "$D_ID" "$D_VER" "$KMM" "$ARCH" 2>/dev/null || true)"
    IFS=$'\t' read -r status notes req <<<"${line:-unknown	no output	}"
    MATCH_STATUS="${status:-unknown}"
    MATCH_NOTES="${notes:-}"
    MATCH_REQ="${req:-}"
    case "$MATCH_STATUS" in
        pass)
            add info MATCH_PASS "System matches a known-good configuration." ""
            ;;
        partial)
            add warn MATCH_PARTIAL "System matches a partially-supported configuration: ${MATCH_NOTES}" \
                "Proceed with caution; see the note above for caveats."
            ;;
        fail)
            add error MATCH_FAIL "System matches a known-bad configuration: ${MATCH_NOTES}" \
                "Upgrade the kernel/distro, or try a supported entry in known-good-configs.json."
            ;;
        untested)
            add warn MATCH_UNTESTED "Configuration is recognized but untested: ${MATCH_NOTES}" \
                "Proceed and report results (pass/fail) back via a PR to scripts/known-good-configs.json."
            ;;
        unknown|*)
            add warn MATCH_UNKNOWN "No matching entry in known-good-configs.json for ${D_ID} ${D_VER} / kernel ${KMM} / ${ARCH}." \
                "After a successful install, submit a PR appending this combination."
            [[ "$STATUS" == "pass" ]] && STATUS="unknown"
            ;;
    esac
}

# --- rendering -------------------------------------------------------------

render_text() {
    echo
    bold "Qualcomm USB Kernel Drivers - Install Environment Report"; echo
    echo "========================================================"
    echo
    printf "  Distribution     : %s (%s %s)\n" "$D_PRETTY" "$D_ID" "$D_VER"
    printf "  Kernel           : %s (%s)\n"     "$KERNEL" "$KMM"
    printf "  Architecture     : %s\n"          "$ARCH"
    printf "  Package manager  : %s\n"          "$PM"
    printf "  Immutable rootfs : %s\n"          "$([[ $IMMUTABLE -eq 1 ]] && echo yes || echo no)"
    printf "  Secure Boot      : %s"            "$SB"
    [[ "$SB" == "enabled" ]] && printf " (MOK enrolled: %s)" "$MOK"
    echo
    printf "  Kernel headers   : "
    if [[ $HDR_OK -eq 1 ]]; then green "found"; printf " (%s)\n" "$HDR_PATH"
    else red "missing"; echo; fi
    printf "  dkms             : %s\n"          "${DKMS_VER:-not installed}"
    printf "  gcc              : %s\n"          "${GCC_VER:-not installed}"
    printf "  make             : %s\n"          "${MAKE_VER:-not installed}"
    printf "  Conflicting mods : %s\n"          "${CONFLICTS:-none}"
    echo
    printf "  Known-good match : "
    case "$MATCH_STATUS" in
        pass)     green  "$MATCH_STATUS" ;;
        partial)  yellow "$MATCH_STATUS" ;;
        fail)     red    "$MATCH_STATUS" ;;
        untested) yellow "$MATCH_STATUS" ;;
        *)        yellow "$MATCH_STATUS" ;;
    esac
    echo
    [[ -n "$MATCH_NOTES" ]] && printf "                     %s\n" "$MATCH_NOTES"
    echo
    echo "Findings"
    echo "--------"
    if [[ ${#FINDINGS[@]} -eq 0 ]]; then
        green "  All checks passed."; echo
    else
        local f level code msg hint
        for f in "${FINDINGS[@]}"; do
            IFS='|' read -r level code msg hint <<<"$f"
            case "$level" in
                error) red   "  [FAIL] " ;;
                warn)  yellow "  [WARN] " ;;
                info)  green "  [INFO] " ;;
                *)     printf "  [----] " ;;
            esac
            printf "%s: %s\n" "$code" "$msg"
            [[ -n "$hint" ]] && printf "         %s %s\n" "$(blue '>')" "$hint"
        done
    fi
    echo
    printf "Overall status   : "
    case "$STATUS" in
        pass)    green  "PASS" ;;
        partial) yellow "PARTIAL" ;;
        fail)    red    "FAIL" ;;
        unknown) yellow "UNKNOWN" ;;
    esac
    echo; echo
}

render_json() {
    local out
    out+='{'
    out+='"tool":"check-install-env",'
    out+='"schema_version":"1.0.0",'
    out+='"timestamp":"'"$(date -u +%Y-%m-%dT%H:%M:%SZ)"'",'
    out+='"status":"'"$(je "$STATUS")"'",'
    out+='"system":{'
    out+='"distro_id":"'"$(je "$D_ID")"'",'
    out+='"distro_version":"'"$(je "$D_VER")"'",'
    out+='"distro_pretty":"'"$(je "$D_PRETTY")"'",'
    out+='"kernel":"'"$(je "$KERNEL")"'",'
    out+='"kernel_major_minor":"'"$(je "$KMM")"'",'
    out+='"arch":"'"$(je "$ARCH")"'",'
    out+='"package_manager":"'"$(je "$PM")"'",'
    out+='"immutable_rootfs":'"$([[ $IMMUTABLE -eq 1 ]] && echo true || echo false)"','
    out+='"secure_boot":"'"$(je "$SB")"'",'
    out+='"mok_enrolled":"'"$(je "$MOK")"'",'
    out+='"kernel_headers_present":'"$([[ $HDR_OK -eq 1 ]] && echo true || echo false)"','
    out+='"kernel_headers_path":"'"$(je "$HDR_PATH")"'",'
    out+='"dkms_version":"'"$(je "$DKMS_VER")"'",'
    out+='"gcc_version":"'"$(je "$GCC_VER")"'",'
    out+='"make_version":"'"$(je "$MAKE_VER")"'",'
    out+='"conflicting_modules":"'"$(je "$CONFLICTS")"'"'
    out+='},'
    out+='"known_good_match":{'
    out+='"status":"'"$(je "$MATCH_STATUS")"'",'
    out+='"notes":"'"$(je "$MATCH_NOTES")"'",'
    out+='"required_packages":"'"$(je "$MATCH_REQ")"'"'
    out+='},'
    out+='"findings":['
    local first=1 f level code msg hint
    for f in "${FINDINGS[@]}"; do
        IFS='|' read -r level code msg hint <<<"$f"
        [[ $first -eq 0 ]] && out+=','
        first=0
        out+='{'
        out+='"level":"'"$(je "$level")"'",'
        out+='"code":"'"$(je "$code")"'",'
        out+='"message":"'"$(je "$msg")"'",'
        out+='"hint":"'"$(je "$hint")"'"'
        out+='}'
    done
    out+=']}'
    printf '%s\n' "$out"
}

# --- main ------------------------------------------------------------------

detect_distro
detect_kernel_arch
detect_secure_boot
detect_headers
detect_toolchain
detect_conflicts
match_known_good

if [[ "$MODE" == "json" ]]; then
    json_out="$(render_json)"
    printf '%s\n' "$json_out"
    if [[ -n "$REPORT_FILE" ]]; then
        printf '%s\n' "$json_out" > "$REPORT_FILE"
    fi
else
    render_text
    if [[ -n "$REPORT_FILE" ]]; then
        render_json > "$REPORT_FILE"
        echo "JSON report written to: $REPORT_FILE"
    fi
fi

case "$STATUS" in
    pass)    code=0 ;;
    partial) code=1 ;;
    fail)    code=2 ;;
    unknown) code=3 ;;
    *)       code=4 ;;
esac

if [[ "$STRICT" -eq 1 && "$STATUS" != "pass" ]]; then
    exit "$code"
fi
exit "$code"
