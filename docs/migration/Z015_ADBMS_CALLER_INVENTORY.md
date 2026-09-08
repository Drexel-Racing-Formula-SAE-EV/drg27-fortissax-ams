# Z-015 ADBMS Caller / Ownership Inventory

## Why this inventory exists

The v2.6.27 FreeRTOS design allows multiple execution paths to reach ADBMS operations and protects the complete logical operation with a recursive priority-inheriting mutex.  Zephyr deliberately replaces that architecture with a **single ADBMS owner**.  That is safer only if diagnostics and service paths are prevented from quietly bypassing the owner.

This document therefore freezes the migration dependency before those commands are ported.

## Current FreeRTOS caller classes

The frozen tree contains transport-using operations from at least these classes:

- periodic ADBMS acquisition/task work;
- ADBMS startup/configuration and config readback;
- cell/S-ADC/C-ADC capture and conversion timing diagnostics;
- SID/status/flag/raw-register reads;
- diagnostic refresh and CS-comparison capture;
- SPI probe/scope/timing service commands;
- explicit wake/cold-wake diagnostics;
- open-wire and auxiliary/GPIO diagnostic paths;
- temperature-bus idle/scan/debug capture;
- recovery/resynchronization operations;
- ADBMS2950/APM SPI probe, config, status, sample, redundant-sample and recovery paths;
- CLI code that explicitly takes `adbms_spi_lock()` around compound operations.

Representative direct CLI/service call sites are concentrated in `Core/Src/tasks/cli_task.c`; the file contains explicit ADBMS lock/unlock sections and many protocol-level calls that eventually reach SPI.  Their existence is the reason the FreeRTOS recursive mutex cannot simply be deleted without replacing ownership semantics.

## Zephyr ownership rule

Z-015 creates the transport but has **zero runtime transfer callers**.  `ams_adbms_spi_platform_init()` is a startup/platform lifecycle operation; it is not an ADBMS actor and emits no transfer.

Before any later migrated CLI/service command may cause an ADBMS operation, the following gate must exist:

```text
CLI / service / diagnostics
          |
          | request
          v
     ADBMS owner queue
          |
          v
      ADBMS owner
          |
          v
 protocol/private transport
          |
          v
         SPI6
          |
          | result
          v
        client
```

The future request object must provide bounded request/result ownership (request kind, arguments, request identity, result/status).  A client timing out while waiting must not be interpreted as cancellation of an already-running transport operation; the owner remains responsible for completing/recovering that operation.

## Machine-enforced rule

The raw transfer header is private under `drivers/ams/`.  At Z-015 only its target implementation may include it; there are no production transfer callers.  When the owner/protocol layer lands later, the source contract must whitelist only that exact owner/protocol location.  Application, CLI, diagnostics, and unrelated platform files may never include or call the private transport directly.

This is an explicit **ADBMS-OWNER-RPC-GATE** dependency for Z-016/Z-017 diagnostics.  "Diagnostic only" is not an exception.

## Deferred work, not Z-015 scope

This inventory does not port the request queue, the CLI commands, wake behavior, protocol operations, acquisition, or APM.  It prevents those later ports from weakening the single-owner architecture.
