#ifndef FAKE_ZEPHYR_DEVICETREE_H_
#define FAKE_ZEPHYR_DEVICETREE_H_
#define DT_PATH(name) 1
#define DT_NODE_HAS_PROP(node, prop) 1
#define DT_PROP_LEN(node, prop) 2
#define DT_NODELABEL(name) DT_NODELABEL_##name
#define DT_NODELABEL_adc1 11
#define DT_NODELABEL_adc2 12
#define DT_SAME_NODE(a, b) ((a) == (b))
#define DT_PROP(node, prop) 6
#endif
