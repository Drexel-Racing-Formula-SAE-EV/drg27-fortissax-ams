#ifndef ZEPHYR_DEVICETREE_H_
#define ZEPHYR_DEVICETREE_H_
#define DT_NODELABEL(name) name
#define DT_NODE_EXISTS(node) 1
#define DT_NODE_HAS_PROP(node, prop) 1
#define DT_SAME_NODE(a, b) ((a) == (b))
#define DT_GPIO_CTLR(node, prop) gpioe
#define DT_GPIO_PIN(node, prop) 0U
#define DT_GPIO_FLAGS(node, prop) 0U
#define ams_safety_io 1
#define gpioe 2
#endif
