# Qcom Driver Always-On Logging Tools

This directory contains the user-mode tooling that supports the always-on,
ring-buffer-based logging design described in
[`docs/always-on-logging-plan.md`](../../docs/always-on-logging-plan.md).

The goal is simple: **every machine that has a Qcom USB kernel driver
installed should be silently recording a bounded recent history of driver
traces, and any app (or a field engineer) can trigger a one-shot "Save Log"
that rotates the buffer into a timestamped file for post-processing.**

## What ships here

```
tools/logging/
├── README.md                      # this file
├── windows/
│   ├── Install-QcomLogging.ps1    # one-time setup: create AutoLogger, seed providers
│   ├── Uninstall-QcomLogging.ps1  # tear down the AutoLogger session
│   ├── Save-QcomDriverLog.ps1     # stop -> copy -> start; returns path to new .etl
│   ├── QcomDrivers.wprp           # WPR/xperf profile for on-demand captures
│   └── QcomTraceProviders.psd1    # provider GUID table (single source of truth)
└── linux/
    ├── qcomlog                    # bash CLI: install | save | status | uninstall
    ├── qcomlogd.service           # systemd unit — provisions ftrace instance at boot
    ├── qcomlog.conf               # tunables (buffer size, retention, etc.)
    └── qcom-diag.rules            # udev rule granting `qcom-diag` group access
```

## Phase coverage (v1)

This PR delivers **Phase 1 + Phase 2 groundwork** from the plan:

| Item                                                       | Status |
|------------------------------------------------------------|--------|
| Shared provider catalogue                                  | ✅ `QcomTraceProviders.psd1`  |
| Windows AutoLogger (circular 64 MB `.etl`)                 | ✅ `Install-QcomLogging.ps1`  |
| Windows "Save ETL" workflow                                | ✅ `Save-QcomDriverLog.ps1`   |
| Windows WPR profile                                        | ✅ `QcomDrivers.wprp`         |
| Linux bounded ftrace instance                              | ✅ `qcomlogd.service` + `qcomlog install`  |
| Linux "Save log" workflow                                  | ✅ `qcomlog save`             |
| Driver INF `AddReg` integration                            | 🟡 follow-up PR (WDK rebuild required — patch snippets in plan doc) |
| Linux tracepoint conversion (`trace_qcom_log`)             | 🟡 follow-up PR (requires kernel header rebuild of each module) |
| `QcomLogSvc` native Windows service (C++/.NET)             | 🟡 follow-up PR (MSBuild project)  |

The two yellow items require a kernel / WDK build to verify; they are kept
separate so this PR itself is **risk-free to the driver binaries** (no driver
source files change).

## Quick start

### Windows

```powershell
# As Administrator:
Set-ExecutionPolicy -Scope Process Bypass
cd <repo>\tools\logging\windows
.\Install-QcomLogging.ps1              # one-time: creates QcomDrivers AutoLogger
# --- reboot once so kernel session starts at SERVICE_SYSTEM_START ---

# any time you reproduce an issue:
.\Save-QcomDriverLog.ps1 -OutDir C:\QcomLogs
# -> C:\QcomLogs\QcomDrivers-20250101T120000Z.etl  (logging resumes automatically)
```

Decode the captured ETL (no PDBs needed if TMFs are installed):

```powershell
tracefmt.exe -nosummary -p %SystemRoot%\Tracing\Qcom -o out.txt C:\QcomLogs\QcomDrivers-20250101T120000Z.etl
```

### Linux

```bash
sudo ./qcomlog install       # installs systemd unit + udev rule
sudo systemctl enable --now qcomlogd.service
# optional: sudo usermod -aG qcom-diag $USER && newgrp qcom-diag

# any time you reproduce an issue:
qcomlog save -o ~/qcomlogs
# -> ~/qcomlogs/qcom-drivers-20250101T120000Z.tar.gz
```

## Design pointers

- Windows: a single ETW session named **`QcomDrivers`** is provisioned as a
  kernel-mode **AutoLogger** so it starts at `SERVICE_SYSTEM_START` and runs
  for the lifetime of the boot. `LogFileMode = 0x00000102`
  (`EVENT_TRACE_FILE_MODE_CIRCULAR`) + `MaxFileSize = 64 MB` gives a bounded
  `.etl` that wraps in place. All Qcom WPP provider GUIDs are enrolled.
- Linux: a single ftrace instance at
  `/sys/kernel/tracing/instances/qcom_drivers/` with `buffer_size_kb = 4096`
  per CPU and `overwrite = 1` produces the same circular behaviour. It is
  event-enabled by `events/qcom/enable` once the driver tracepoints land
  (Phase 2). Until then the instance also captures `usb_*` and `module_*`
  events which already cover the qcom code paths.
- Save flow: **stop → rotate → start**, gap < 200 ms on Windows, < 50 ms on
  Linux. Boundary events are emitted before/after so offline tools can stitch
  two consecutive saves chronologically.

See [`docs/always-on-logging-plan.md`](../../docs/always-on-logging-plan.md)
for the full design, performance targets, risks, and delivery phasing.