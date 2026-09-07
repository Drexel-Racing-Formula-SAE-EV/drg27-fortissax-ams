#ifndef ZEPHYR_DEVICETREE_H_
#define ZEPHYR_DEVICETREE_H_
#define ams_imd 100
#define pwm2 200
#define gpioc 300
#define status_gpios 1
#define DT_NODELABEL(name) name
#define DT_NODE_EXISTS(node) 1
#define DT_NODE_HAS_PROP(node, prop) 1
#define DT_GPIO_CTLR(node, prop) gpioc
#define DT_GPIO_PIN(node, prop) 5
#define DT_GPIO_FLAGS(node, prop) 0U
#define DT_SAME_NODE(a, b) ((a) == (b))
#endif
