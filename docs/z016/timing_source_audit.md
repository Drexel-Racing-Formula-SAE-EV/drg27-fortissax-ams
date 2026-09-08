# Pinned Zephyr timing source audit

Revision: v4.4.0.

- `drivers/timer/Kconfig.cortex_m_systick` SHA256 `879245953a4a82ac05299b92af7a86867fcb21feca3e5bb6225f8fe675d1cf0e`
- `drivers/timer/cortex_m_systick.c` SHA256 `b1650fac1fb760d1857324481437988458340a232d6df1a3aa4fe8085ca6b4a7`
- `include/zephyr/drivers/timer/system_timer.h` SHA256 `8f6829ccca5e72da650a7ce8d735e4c9484327a052a8770cce69e03ffa979e3d`
- `include/zephyr/kernel.h` SHA256 `83666b134350dfd5be53b9aad50cdc018834371dc32bb8ef9c2350239e9a7f23`
- `include/zephyr/kernel/thread.h` SHA256 `cc9128b19b8b1485774233e9bdd49d6c030b033a7a934eab70afaa4f3e39f9fa`
- `include/zephyr/sys/time_units.h` SHA256 `ab708f3650fe960c6679c323be1c4c21c7da056c540aa0f628d4e2254c89c5bf`
- `include/zephyr/sys_clock.h` SHA256 `0ecdeba570b0976ba9e9b5fe5f25d44e695955d4bf1ef0f0c281e69be61d6b47`
- `include/zephyr/timing/timing.h` SHA256 `14d0733d9a61209d4c13a521945a2f41daefba0f6f37fbeb37cb30173ef169b6`

The SysTick elapsed read has no polling loop. The 64-bit capability and cycle/unit conversion APIs exist at this revision. Target compilation and physical timing remain open.
