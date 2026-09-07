#ifndef FAKE_ZEPHYR_IO_CHANNELS_H_
#define FAKE_ZEPHYR_IO_CHANNELS_H_
#define DT_IO_CHANNELS_CTLR_BY_IDX(node, idx) ((idx) == 0 ? 11 : 12)
#define DT_IO_CHANNELS_INPUT_BY_IDX(node, idx) ((idx) == 0 ? 3 : 10)
#endif
