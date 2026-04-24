# Always-On Logging for Qcom USB Kernel Drivers — Implementation Plan

## 1. Problem Statement

Today, logging in the kernel open-source drivers (Windows WPP, Linux `printk`) is **opt-in**:

- End users / field engineers must install a debug build or manually enable trace
  sessions (`logman`, `tracelog`, `trace-cmd`, `echo 1 > /sys/.../parameters/debug`,
  dynamic_debug, etc.) **before** the issue is reproduced.
- Most issues are reported *after* the fact. By the time the trace is enabled, the
  original failing sequence is already gone.
- Each driver (qcfilter, qcusbnet, qdbusb, qcwdfserial on Windows; qcom_serial,
  qcom_usb, qcom_usbnet on Linux) has its own enable/collect procedure, so
  customers must learn many workflows. This lengthens every debug cycle.

**Goal:** every shipping driver must have an **always-on, bounded ring-buffer
trace** session running from boot. When an application wants a snapshot, a single
"Save Log" action rotates the buffer to a file and starts a new one. No driver
rebuild, no registry/command changes required in the field.

## 2. High-Level Design

| Aspect                  | Windows (WPP / ETW)                                              | Linux (ftrace)                                              |
|-------------------------|------------------------------------------------------------------|-------------------------------------------------------------|
| Backing buffer          | ETW kernel-mode session, **FileMode = Circular**                 | ftrace instance with bounded `buffer_size_kb` (per-cpu)     |
| Always-on mechanism     | **AutoLogger** registry keys started at `SERVICE_SYSTEM_START`   | systemd unit `qcomlogd.service` enabled at boot             |
| Snapshot ("Save ETL")   | `StopTrace` → copy/rename `.etl` → `StartTrace` (<200 ms gap)    | `snapshot` + copy from `/sys/kernel/tracing/instances/...`  |
| Trigger API             | User-mode service `QcomLogSvc` + named pipe + CLI                | User-mode helper `qcomlogd` + UNIX socket + CLI             |
| Providers               | Existing per-driver WPP GUIDs, grouped under one AutoLogger      | New `qcom_*` tracepoints; `QC_LOG_*` macros redirect to them |
| Output format           | `qcomdrv-YYYYMMDDTHHMMSS.etl`                                    | `qcomdrv-YYYYMMDDTHHMMSS.tar.gz` (trace + symbols)          |

Design principle: **no extra code in hot paths**. Existing `QCSER_DbgPrint` /
`QC_LOG_*` macros stay. We only change (a) how/when tracing is started,
(b) how snapshots are captured, (c) keep the buffer bounded and circular so
impact is negligible.

## 3. Windows Implementation

### 3.1 Current state (survey)

| Driver module | Folder       | INF(s)                         | WPP header        | EVENT_TRACING release default |
|---------------|--------------|--------------------------------|-------------------|-------------------------------|
| qcfilter      | `src/windows/filter`    | `qcfilter.inf`         | `qcfilterwpp.h`   | yes                           |
| qcusbnet      | `src/windows/ndis`      | `qcwwan.inf`           | `MPWPP.h`         | yes                           |
| qdbusb        | `src/windows/qdss`      | `qdbusb.inf`           | `QDBWPP.h`        | yes                           |
| qcwdfserial   | `src/windows/wdfserial` | `qcwdfser.inf`, `qcwdfmdm.inf` | `QCWPP.h`  | yes                           |
| qcadb         | `src/windows/qcadb`     | `qcadb.inf` (NullDriver) | n/a              | n/a                           |

Observed: all four coded drivers already define WPP control GUIDs and are built
with `EVENT_TRACING`, `<WppEnabled>true</WppEnabled>` for every config
(Debug/Release × x64/ARM64/…). What's missing: an AutoLogger config and a
standard "save" workflow.

GUIDs to collect (qcwdfserial known today — others to be extracted in Step 1):

| Driver     | WPP Control GUID                                  |
|------------|---------------------------------------------------|
| qcwdfser   | `{5F02CA82-B28B-4a95-BA2C-FBED52DDD3DC}` (QCUSB)  |
| qcfilter   | _TBD — read from `qcfilterwpp.h`_                 |
| qcusbnet   | _TBD — read from `MPWPP.h`_                       |
| qdbusb     | _TBD — read from `QDBWPP.h`_                      |

### 3.2 Unified provider catalogue

- New file `src/windows/common/QcomTraceProviders.h` — single header listing
  every driver's GUID and default keyword mask, consumed by installers and the
  user-mode service.
- New file `tools/windows/QcomDrivers.wprp` — Windows Performance Recorder
  profile enumerating the same providers, so engineers can trigger captures
  from WPR/xperf when needed.

### 3.3 AutoLogger configuration (always-on, circular)

Add an `AddReg` stanza in every driver INF that writes to the shared
`QcomDrivers` AutoLogger session. Example (to be mirrored in `qcfilter.inf`,
`qcwwan.inf`, `qdbusb.inf`):

```ini
[QCSerial_AddReg_AutoLogger]
HKLM,"System\CurrentControlSet\Control\WMI\AutoLogger\QcomDrivers",Start,0x00010001,1
HKLM,"System\CurrentControlSet\Control\WMI\AutoLogger\QcomDrivers",GUID,0x00000000,"{A1B2C3D4-0000-0000-0000-QCOMDRIVERS}"
HKLM,"System\CurrentControlSet\Control\WMI\AutoLogger\QcomDrivers",LogFileMode,0x00010001,0x00000102  ; EVENT_TRACE_FILE_MODE_CIRCULAR
HKLM,"System\CurrentControlSet\Control\WMI\AutoLogger\QcomDrivers",FileName,0x00000000,"%SystemRoot%\Tracing\QcomDrivers.etl"
HKLM,"System\CurrentControlSet\Control\WMI\AutoLogger\QcomDrivers",MaxFileSize,0x00010001,64   ; MB circular cap
HKLM,"System\CurrentControlSet\Control\WMI\AutoLogger\QcomDrivers",BufferSize,0x00010001,64    ; KB per buffer
HKLM,"System\CurrentControlSet\Control\WMI\AutoLogger\QcomDrivers",MinimumBuffers,0x00010001,8
HKLM,"System\CurrentControlSet\Control\WMI\AutoLogger\QcomDrivers",MaximumBuffers,0x00010001,16

; one sub-key per provider GUID
HKLM,"System\CurrentControlSet\Control\WMI\AutoLogger\QcomDrivers\{5F02CA82-B28B-4a95-BA2C-FBED52DDD3DC}",Enabled,0x00010001,1
HKLM,"System\CurrentControlSet\Control\WMI\AutoLogger\QcomDrivers\{5F02CA82-B28B-4a95-BA2C-FBED52DDD3DC}",EnableLevel,0x00010001,4            ; TRACE_LEVEL_INFORMATION
HKLM,"System\CurrentControlSet\Control\WMI\AutoLogger\QcomDrivers\{5F02CA82-B28B-4a95-BA2C-FBED52DDD3DC}",MatchAnyKeyword,0x0001000A,0xFFFFFFFFFFFFFFFF
```

Rationale:
- `LogFileMode=0x00000102` = `EVENT_TRACE_FILE_MODE_CIRCULAR |
  EVENT_TRACE_USE_GLOBAL_SEQUENCE` → a bounded `.etl` that wraps in place.
- `MaxFileSize = 64 MB` → hours of INFO or days of WARN/ERR at typical rates.
- Keyword mask per provider means field teams can narrow flags via registry
  without a rebuild. Chatty data-path keywords
  (`WPP_DRV_MASK_TDATA`, `WPP_DRV_MASK_RDATA`) default **off** so the circular
  buffer keeps more history.

### 3.4 "Save ETL" — user-mode service `QcomLogSvc`

New Windows service (C++17, no external deps) packaged with the drivers.

Responsibilities:
1. Register the `QcomDrivers` AutoLogger if it's missing (self-heal) and start
   it at service start if it's not already running (handles pre-AutoLogger OS
   snapshots / upgrade cases).
2. Expose a **single public API `SaveSnapshot(outputPath, options)`** over:
   - Named pipe `\\.\pipe\QcomLogSvc` (JSON request/response)
   - CLI `qcomlog.exe save [--out PATH] [--bundle]`
   - COM interface `IQcomLogSvc` for in-proc apps (e.g., QUTS, QDS)
   - PowerShell cmdlet `Save-QcomDriverLog` (wraps CLI)
3. Snapshot flow:
   1. `ControlTrace(..., EVENT_TRACE_CONTROL_FLUSH)` to flush pending buffers.
   2. `ControlTrace(..., EVENT_TRACE_CONTROL_STOP)` — closes `.etl`.
   3. Move/rename target file:
      `%SystemRoot%\Tracing\QcomDrivers.etl` →
      `<outputPath>\QcomDrivers-YYYYMMDDTHHMMSSZ.etl`
   4. `StartTrace(...)` with the cached AutoLogger template to resume logging
      immediately. **Target gap < 200 ms** (measurable by pairing boundary
      markers emitted by the service's own provider).
   5. If `--bundle`: also collect `setupapi.dev.log`, `devnodeclean` output,
      `pnputil /enum-drivers`, driver INFs, relevant TMFs and produce
      `QcomDrivers-YYYYMMDDTHHMMSSZ.zip`.
4. Telemetry: event-log source `QcomLogSvc` records snapshot count, bytes,
   duration, last error.
5. Security:
   - Service runs as `LocalSystem` (needed to control the kernel ETW session).
   - Pipe ACL: `LocalAdmins` and members of a new local group
     `QcomDriverDiagnostics`, so non-admin helpdesk accounts can be enrolled.
   - Optional `--allow-user-save` registry knob.

### 3.5 Driver-side changes (minimal)

- Confirm every release build defines `EVENT_TRACING` (already true — verified
  in each `*.vcxproj`).
- Ensure each driver's `.tmh` generation places TMF files into the package so
  `tracefmt.exe` can decode `.etl` on a different machine without PDBs. Install
  them to `%SystemRoot%\Tracing\Qcom\<driver>.tmf`.
- Re-audit every `QCSER_DbgPrint` and `QCSER_DbgPrintX` call so that high-rate
  per-byte traces are tagged with `WPP_DRV_MASK_TDATA`/`WPP_DRV_MASK_RDATA`
  keywords only; AutoLogger default keyword mask excludes these.
- Emit a small `Boundary` event at trace start (driver load) and from
  `QcomLogSvc` before/after the Stop/Start sequence so post-processing can
  stitch rotated `.etl`s chronologically.

### 3.6 Tooling deliverables (Windows)

- `tools/windows/QcomDrivers.wprp` — WPR/xperf profile.
- `tools/windows/Save-QcomDriverLog.ps1` — thin wrapper calling the service.
- `tools/windows/Decode-Etl.ps1` — invokes `tracefmt.exe` with the shipped
  TMFs to convert `.etl` → `.txt` on any machine.
- `tools/windows/QcomLogSvc/` — service source (MSBuild project).

## 4. Linux Implementation

### 4.1 Current state (survey)

All three Linux modules log with `printk` via wrapper macros:

| Module          | Source files                                | Log macros                              | Gate                                                           |
|-----------------|---------------------------------------------|-----------------------------------------|----------------------------------------------------------------|
| qcom_serial     | `qcom_serial.c/.h`                          | `DBG`, `GobiDBG`, `GOBI_DBG`            | `debug` module_param                                           |
| qcom_usb        | `qcom_usb_main.c`, `qtiDevInf.h`            | `QC_LOG_DBG/INFO/WARN/ERR/...`          | per-device `->debug` + global `debug_g`, and `->logLevel`      |
| qcom_usbnet     | `qcom_usbnet_main.c`, `qmidevice.c`, `qmi.c`, `qtiDevInf.h` | `QC_LOG_*`, `QC_LOG_AGGR`  | per-device `->debug` (1 or 0xFF) + `->logLevel` + `debug_aggr` |

Nothing is captured unless the user explicitly sets `debug=1` / changes
`logLevel`, raises `dmesg` buffer size, or keeps a `journalctl -f` session
open. There are no tracepoints, no debugfs endpoints, no kfifo ring buffers.

### 4.2 Target mechanism: dedicated ftrace instance

Linux already ships a production-grade bounded ring buffer. A per-package
instance lives at `/sys/kernel/tracing/instances/<name>/` with its own
per-CPU buffer, `trace_pipe`, `snapshot`, `buffer_size_kb`, and event enable
mask. We provision a single instance `qcom_drivers`:

```
/sys/kernel/tracing/instances/qcom_drivers/
    buffer_size_kb   = 4096    # per CPU → ≈ 32 MB on an 8-core laptop
    tracing_on       = 1
    events/qcom/enable = 1
    events/qcom/qcom_log_dbg/enable = 0   # default off, saves buffer space
    options/overwrite = 1                 # circular (overwrite oldest)
```

### 4.3 Tracepoint conversion

Add a common header `src/linux/common/trace/qcom_drv_trace.h` that defines
the tracepoint once per driver (idiomatic `CREATE_TRACE_POINTS`):

```c
TRACE_EVENT(qcom_log,
    TP_PROTO(const char *drv, int level, const char *func, int line,
             struct va_format *vaf),
    TP_ARGS(drv, level, func, line, vaf),
    TP_STRUCT__entry(
        __string(drv,  drv)
        __field(int,   level)
        __string(func, func)
        __field(int,   line)
        __vstring(msg, vaf->fmt, vaf->va)
    ),
    TP_fast_assign(
        __assign_str(drv,  drv);
        __entry->level = level;
        __assign_str(func, func);
        __entry->line  = line;
        __assign_vstr(msg, vaf->fmt, vaf->va);
    ),
    TP_printk("[%s] %s:%d L%d %s",
        __get_str(drv), __get_str(func), __entry->line,
        __entry->level, __get_str(msg))
);
```

Redirect `QC_LOG(...)` in `qtiDevInf.h` so that in addition to `printk` it
emits the tracepoint unconditionally (cost ≈ a few ns when the tracepoint is
disabled, the usual ftrace static-key pattern):

```c
#define QC_LOG(KERN_LVL, DEV, fmt, lvl, ver, ...) do {                    \
    struct va_format _vaf = { .fmt = fmt };                               \
    trace_qcom_log(DEV ? DEV->mdeviceName : "qcom",                       \
                   lvl, __func__, __LINE__, &_vaf, ##__VA_ARGS__);        \
    if (DEV && (DEV->debug == 1 || DEV->debug == 0xFF) &&                 \
        (DEV->logLevel <= lvl))                                           \
        printk(KERN_LVL "%s: %s:%d %s " fmt,                              \
               DEV->mdeviceName, __func__, __LINE__, ver, ##__VA_ARGS__); \
} while (0)
```

Key properties:
- Tracepoints are **always on** into the circular ring regardless of the
  `debug` / `logLevel` knobs — that's what makes logging always-on.
- `printk` behaviour is preserved for users who rely on `dmesg`, but becomes
  essentially a fallback.
- `DBG()` / `GobiDBG()` / `GOBI_DBG()` in `qcom_serial` get thin wrappers that
  funnel through the same tracepoint.

### 4.4 "Save log" — user-mode helper `qcomlogd`

Small C daemon + CLI packaged with the drivers (`tools/linux/qcomlogd/`).

- `systemd` unit `qcomlogd.service` (with `After=sys-kernel-tracing.mount`)
  provisions the instance at boot:
  1. `mkdir /sys/kernel/tracing/instances/qcom_drivers`
  2. Write `4096` to `buffer_size_kb`.
  3. Write `1` to `options/overwrite`, `events/qcom/enable`, `tracing_on`.
- Listens on UNIX socket `/run/qcomlogd.sock` (JSON protocol) and SIGUSR1
  (save with default path).
- CLI: `qcomlog save [--out PATH]` (calls into the daemon over the socket so
  non-root apps with `qcom-diag` group membership can trigger a save).
- **Save flow**:
  1. Briefly `echo 0 > tracing_on`.
  2. Copy `trace` (ASCII) and `trace_raw` (if enabled) into
     `<out>/qcom-drivers-YYYYMMDDTHHMMSSZ/` along with `available_events`,
     `saved_cmdlines`, `kernel.version`, and an `instance_config` dump.
  3. Also capture last `N` lines of `dmesg --ctime` (fallback for sites
     that haven't migrated everything to tracepoints).
  4. `echo 1 > tracing_on` (gap target < 50 ms).
  5. `tar -czf qcom-drivers-TS.tar.gz` the directory.
- Rotation: daemon keeps the most recent 10 snapshots by default.
- Permissions: daemon runs as root; the socket is group-owned by
  `qcom-diag`.

### 4.5 Kbuild / packaging changes

- `src/linux/Makefile`: add `-I$(src)/../common/trace` and `CREATE_TRACE_POINTS`
  in one `.c` per module (e.g., `qcom_trace.c`).
- Debian / RPM specs ship `qcomlogd`, the `systemd` unit, and udev rule that
  also fires `qcomlogd reload` when a driver module loads (for hotplug
  scenarios where the instance needs re-application on very old kernels that
  don't persist instance buffers).
- Minimum kernel: `5.4` (instances, snapshot, and `vstring` all available).

### 4.6 Tooling deliverables (Linux)

- `tools/linux/qcomlogd/` — daemon source (C) + CLI.
- `tools/linux/qcomlogd.service` — systemd unit.
- `tools/linux/qcom-diag.rules` — udev + polkit rule to let the
  `qcom-diag` group talk to the daemon.
- `tools/linux/Decode-Trace.sh` — convenience wrapper around
  `trace-cmd report` that understands the saved tarball layout.

## 5. Cross-platform consistency

- **Public API shape is identical**: `SaveSnapshot(outputPath) → file path`
  returned to the caller, reasons for failure enumerated the same way
  (e.g., `ALREADY_IN_PROGRESS`, `DISK_FULL`, `PERMISSION_DENIED`).
- **File layout is identical**: one timestamped top-level artifact per save;
  Windows ships `.etl`, Linux ships `.tar.gz` whose top-level directory has
  the same timestamp.
- **Shared schema for metadata** (`manifest.json`) in both: driver versions,
  OS version, host name, machine id, time range covered, keywords enabled.
- Qualcomm apps (QUTS, QDS, user-mode tools) link a thin abstraction
  `QcomLog::SaveSnapshot()` that dispatches to `QcomLogSvc` on Windows and
  `qcomlogd` on Linux.

## 6. Performance targets / acceptance criteria

| Metric                                                | Target                          |
|-------------------------------------------------------|---------------------------------|
| Peak CPU overhead of always-on session                | ≤ 0.5% per core @ 10 kEvt/s     |
| Sustained memory footprint                            | ≤ 32 MB Windows, ≤ 32 MB Linux  |
| Save latency (API call → file closed)                 | ≤ 1 s                           |
| Logging gap during save                               | ≤ 200 ms Windows, ≤ 50 ms Linux |
| History retained by default                           | ≥ 30 min heavy I/O INFO traces  |
| Works without internet / PDBs on customer machine     | yes (TMFs shipped, symbols bundled) |

## 7. Phased delivery plan

### Phase 0 — Discovery & spec freeze (1 week)
- Inventory every WPP GUID / keyword / level across all four Windows drivers;
  fill in the "TBD" rows in §3.1.
- Decide the shared `QcomDrivers` AutoLogger GUID.
- Confirm oldest kernel families we must support for the Linux ftrace-instance
  approach (target: RHEL 8/9, Ubuntu 20.04+).
- Deliverable: this doc finalized + `QcomTraceProviders.h` skeleton.

### Phase 1 — Windows AutoLogger + service (3 weeks)
- Add `[*_AddReg_AutoLogger]` sections to all four driver INFs.
- Implement `QcomLogSvc` (service + CLI + COM + PS cmdlet).
- Ship TMFs alongside binaries.
- Internal dogfood on CI lab fleet.

### Phase 2 — Linux tracepoints + daemon (3 weeks)
- Add `qcom_log` tracepoint and plumb through `QC_LOG` / `DBG` macros.
- Build `qcomlogd` + systemd unit + packaging (`.deb`, `.rpm`).
- Verify on Ubuntu, RHEL, and the in-house yocto board.

### Phase 3 — Integration (2 weeks)
- QUTS and QDS wire "Save Log" buttons to `SaveSnapshot()`.
- Add upload path to customer bug-report workflow.
- Documentation: update `docs/windows/getting-started/README.md` and create
  `docs/linux/getting-started/README.md` with a "how to collect a log" section
  that is now one command.

### Phase 4 — Hardening & rollout (2 weeks)
- Fuzz the IPC surfaces (pipe / socket).
- Long-soak (72 h) with heavy USB traffic — verify ring bounded, no leaks,
  verify 1000+ Save cycles.
- Ship in next driver MR + tools release.

## 8. Open questions / risks

1. **ETW AutoLogger on S-mode / HVCI-locked systems** — confirm the INF-driven
   registry write is permitted. Fallback: let `QcomLogSvc` create the session
   via API at service start.
2. **Very old Linux kernels (<5.4) without `__vstring`** — fall back to
   `__dynamic_array(char, msg, 256)` with `vsnprintf` in `TP_fast_assign`.
3. **Kernel lockdown mode (Secure Boot, LOCKDOWN_TRACEFS)** — tracefs can be
   restricted; `qcomlogd` must detect and fall back to `dmesg` + rate-limited
   kfifo in a module param. Acceptable degraded mode but not the primary path.
4. **Storage on embedded systems** — 32 MB RAM + 64 MB disk may be too much on
   small IoT targets; expose `buffer_size_kb` / `MaxFileSize` via module
   parameter / registry for per-SKU tuning.
5. **PII in traces** — audit current `QC_LOG_DATA` / `WPP_DRV_MASK_RDATA`
   payloads; those must stay behind a non-default keyword that customers
   explicitly enable before reproducing.

## 9. Summary

By combining:
- existing WPP / `printk` code (no hot-path changes),
- OS-native always-on ring buffers (ETW AutoLogger, ftrace instances),
- a thin privileged helper (`QcomLogSvc`, `qcomlogd`) exposing a single
  `SaveSnapshot` API,

every Qcom kernel driver gets **continuous, bounded, zero-setup logging**.
The customer debug cycle shrinks from "reinstall debug driver + reproduce +
manually collect" to a **single Save Log click**, with the last N minutes of
context already captured.
