#
# QcomTraceProviders.psd1
#
# Single source of truth for every WPP provider GUID exposed by the Qualcomm
# USB kernel drivers. Consumed by:
#   - Install-QcomLogging.ps1 (to seed the AutoLogger registry)
#   - Save-QcomDriverLog.ps1  (for logging/metadata)
#   - QcomDrivers.wprp        (kept in sync by hand; see header there)
#
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause
#

@{
    # Shared AutoLogger session identity.
    SessionName    = 'QcomDrivers'
    SessionGuid    = '{A1B2C3D4-0000-4000-8000-0000C0A1C0A1}'  # re-generate with New-Guid before shipping
    LogFileName    = '%SystemRoot%\Tracing\QcomDrivers.etl'

    # Circular, bounded file. 64 MB wraps in place.
    LogFileMode    = 0x00000102   # EVENT_TRACE_FILE_MODE_CIRCULAR | EVENT_TRACE_USE_GLOBAL_SEQUENCE
    MaxFileSizeMB  = 64
    BufferSizeKB   = 64
    MinBuffers     = 8
    MaxBuffers     = 16

    # Default trace level for every provider.
    # 1=CRITICAL 2=ERROR 3=WARNING 4=INFO 5=VERBOSE
    DefaultLevel   = 4

    # Default keyword mask.
    # Chatty data-path keywords (TDATA, RDATA) are intentionally OFF so the
    # 64 MB circular buffer retains more history. A customer who really needs
    # byte-level data traces can enable them via Save-QcomDriverLog -Verbose.
    #
    # Computed as: everything except (TDATA | RDATA | DATA_WT | DATA_RD)
    #              from QCWPP.h / qcfilterwpp.h (bits 6,7,17,18)
    DefaultKeyword = 0xFFFFFFFFFFF9FF3F
    VerboseKeyword = 0xFFFFFFFFFFFFFFFF

    # Every coded driver in the repo that emits WPP traces.
    # NOTE: qcwdfserial/qcfilter share the same GUID today (QCUSB). Once the
    # follow-up PR splits them (see docs/always-on-logging-plan.md §3.1) this
    # table will carry four distinct GUIDs.
    Providers = @(
        @{
            Name    = 'qcwdfserial'
            Guid    = '{5F02CA82-B28B-4a95-BA2C-FBED52DDD3DC}'
            Header  = 'src/windows/wdfserial/QCWPP.h'
            Comment = 'Qcom WDF serial (modem + diag)'
        },
        @{
            Name    = 'qcfilter'
            Guid    = '{5F02CA82-B28B-4a95-BA2C-FBED52DDD3DC}'
            Header  = 'src/windows/filter/qcfilterwpp.h'
            Comment = 'Qcom USB filter driver (shares QCUSB GUID today)'
        },
        @{
            Name    = 'qcusbnet'
            Guid    = '{5F02CA82-B28B-4a95-BA2C-FBED52DDD3DC}'
            Header  = 'src/windows/ndis/MPWPP.h'
            Comment = 'Qcom NDIS miniport (qcusbwwan)'
        },
        @{
            Name    = 'qdbusb'
            Guid    = '{5F02CA82-B28B-4a95-BA2C-FBED52DDD3DC}'
            Header  = 'src/windows/qdss/QDBWPP.h'
            Comment = 'Qcom QDSS bulk'
        }
    )
}