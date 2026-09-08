# Hardened private STM32F767 current-ADC adapter SIL

This host SIL compiles the real `drivers/ams/current_adc_stm32.c` against a
minimal fake Zephyr/STM32-LL surface. It exists because the post-Z-015 HAL
review found that Zephyr v4.4's async STM32 ADC path retains three pieces of
in-flight state after an application-side timeout: the driver `adc_context`
lock/signal, the driver's caller buffer pointer, and the ADC IRQ completion
path. RCC reset alone cannot make those objects safe to reuse.

The hardened adapter therefore keeps generic `CONFIG_ADC=n`, leaves ADC1/ADC2
(and ADC3, because F767 ADCRST is common) disabled as Zephyr devices, and uses a
private bounded polling backend with no ADC ISR, DMA, `k_poll_signal`, or
`adc_context` lifetime.

Covered invariants include:

- exact ADC1_IN3/PA3 high-range then ADC2_IN10/PC0 low-range ordering;
- 108 MHz PCLK2, synchronous /6 = 18 MHz ADC clock, 12-bit, 480-cycle sample;
- software-triggered single conversion and no continuous/DMA/interrupt mode;
- one bounded HAL-parity conversion timeout: timeout clock starts after conversion start, and a timeout is committed only when elapsed is greater than 5 ms with EOC still absent;
- HIGH failure suppresses LOW; LOW failure preserves fresh HIGH only;
- EOC/OVR/enable failures cannot publish a coherent pair;
- timeout/error disables and clears the shared ADC IRQ, performs common RCC
  reset, clears pending NVIC state again, fully reprograms and readback-verifies
  ADC1/ADC2, then permits the next scan to retry;
- recovery failure alone produces terminal adapter `FAULTED` state;
- reentrant access is rejected and durably counted as an integrity violation;
- 50,000 randomized transactions mix nominal, timeout, overrun and enable-fail
  cases while proving recovery counts and non-terminal transient behavior.

The busy poll intentionally contains no `k_yield()`/`k_sleep()`: that preserves
the frozen HAL polling model and avoids adding a scheduler-dependent gap inside
a single conversion. The owner remains a normal preemptible Zephyr thread, so
higher-priority interrupts/tasks can still run.

Physical ADC timing/fault injection remains a target/hardware gate; this SIL is
source/behavioral evidence, not physical validation.
