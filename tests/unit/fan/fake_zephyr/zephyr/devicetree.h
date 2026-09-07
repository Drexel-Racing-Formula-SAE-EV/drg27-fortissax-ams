#ifndef ZEPHYR_DEVICETREE_H_
#define ZEPHYR_DEVICETREE_H_

/* Deterministic fake node identifiers used by the production fan adapter's
 * DT_NODELABEL()/DT_SAME_NODE() BUILD_ASSERT contract. */
#define ams_fans 100
#define pwm3 203
#define pwm4 204
#define pwm5 205

#define DT_NODELABEL(name) name
#define DT_PROP_LEN(node, prop) 6
#define DT_SAME_NODE(a, b) ((a) == (b))

#endif /* ZEPHYR_DEVICETREE_H_ */
