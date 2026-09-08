# Z-015 Zephyr STM32 SPI Driver Audit

## Scope and pin

This audit was performed before implementing the Z-015 transport.  `west.yml` pins **Zephyr v4.4.0**.  The upstream v4.4.0 tag resolves to commit `684c9e8f32e4373a21098559f748f06915f950c9`.

The driver file at this revision is:

`drivers/spi/spi_stm32.c`

There is no `drivers/spi/spi_ll_stm32.c` at this revision.  Any older migration note naming that path is superseded by this document.

## Decisive F767/full-duplex finding

`spi_stm32_complete()` contains:

```c
if (LL_SPI_GetMode(spi) == LL_SPI_MODE_MASTER) {
    while (ll_spi_is_busy(spi) && LL_SPI_IsEnabled(spi)) {
        ...
    }
}
```

The loop body has two possible paths that can disable SPI and terminate the wait:

1. an H7-specific branch for non-FIFO 9/17/25-bit transfers; and
2. a half-duplex-RX branch.

Neither applies to **STM32F767, 8-bit, full-duplex master**, which is the frozen AMS configuration.  `spi_stm32_isr()` calls `spi_stm32_complete()` from interrupt context.  Thus, if the F767 SPI BSY condition remains asserted in this completion path, the stock interrupt-driven driver provides no bounded software escape inside that loop.  The independent IWDG remains an ultimate reset mechanism, but that is a materially different availability behavior from v2.6.27's bounded HAL transport failure.

This part/mode-specific behavior, rather than a generic criticism of all STM32 SPI use, is the decisive reason Z-015 does not use the stock transaction engine.

## `spi_release()` is not an abort primitive

At v4.4.0, the STM32 implementation is effectively:

```c
spi_context_unlock_unconditionally(&data->ctx);
ll_disable_spi(cfg->spi);
```

It does not establish a cancellation generation, invalidate callback/userdata, clear all transaction context, or provide a documented guarantee that an incomplete asynchronous operation can no longer complete later.  The public API documentation describes `spi_release()` as releasing lock/held-CS state, not aborting an arbitrary in-flight transfer.

## Async lifetime audit

The generic SPI async path stores transfer configuration, buffer state, callback, and callback userdata in shared `spi_context`.  `spi_transceive_signal()` is a callback wrapper whose userdata is the `k_poll_signal`; the signal result is completion status, not a transaction-generation field.

A robust adapter around an uncertain async timeout would therefore need to resolve at least three lifetimes before reuse:

1. application transfer storage;
2. completion object/callback identity; and
3. shared driver/controller transaction context.

Per-slot buffers or signals alone cannot prove the third lifetime.  This complexity disappears if the AMS uses a private, synchronous, bounded polling backend.

## Frequency audit

The stock driver obtains the actual STM32 clock through `clock_control_get_rate()` and chooses the first power-of-two prescaler whose achieved clock is no greater than the requested frequency.  For the F7 table, DIV2 is the first entry, so a 108 MHz input and a 421,875-Hz request select DIV256 exactly.

The driver's debug log omits the internal `shift` used in the actual divider calculation.  Therefore its log line can misstate the displayed divider/frequency on this F7 even while the hardware programming is correct.  Z-015 must not use that log as frequency evidence.  The private adapter verifies the APB2 input clock and reads back the programmed BR field itself.

## Audit decision

For the AMS ADBMS transport, reject as the Z-015 architecture:

- stock STM32 interrupt-driven SPI;
- stock async signal/callback wrapping;
- `spi_release()` as an abort mechanism;
- DMA/RTIO transport;
- an application-managed late-completion quarantine system around the stock driver.

Use instead a small private STM32F767 polling backend under `drivers/ams/` while retaining Zephyr ownership of Devicetree, pinctrl, GPIO, RCC clock-control, reset-controller, timing, and build/configuration infrastructure.

## Private-backend safety rules

- `&spi6` remains `status = "disabled"` so the stock Zephyr SPI device is not instantiated.
- `CONFIG_SPI=n` in the current firmware because there is no other SPI consumer.
- STM32 LL SPI support is selected privately by the AMS backend.
- SPI6 NVIC IRQ is never enabled.  Initialization and recovery explicitly disable the line and clear its pending latch.
- No SPI6 DMA, RTIO, async callback, `k_poll_signal`, or stock `spi_transceive()` path exists.
- Polling uses one transaction-level, wrap-safe 500-ms absolute deadline.
- The polling region does not take `irq_lock()`, does not scheduler-lock, and does not insert unproven inter-byte sleeps/yields while CS is asserted.
- Every logical write drains RX bytes because the hardware remains full duplex.
- On timeout/I/O failure: restore both CS lines inactive first, disable/clear IRQ state, reset SPI6 through RCC, reprogram the frozen contract, and verify it by readback.
- Recovery success returns a recoverable operation error and leaves the adapter READY; recovery failure latches FAULTED.

## Remaining hardware-only evidence

This audit does not constitute physical validation.  Oscilloscope confirmation of Mode 3, 421,875 Hz, CS routing, and later ADBMS6822 wake/isoSPI behavior remains open and belongs to the appropriate hardware/later-stage gates.
