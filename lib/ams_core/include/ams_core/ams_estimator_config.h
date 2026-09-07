/*
 * ams_estimator_config.h
 *
 * Portable estimator compile-time configuration for the DRG27 Zephyr
 * migration. The estimator math itself is copied from the frozen
 * DER26 v2.6.27 / firmware 0.5.30 oracle.
 *
 * The legacy source obtained these topology identifiers and its default
 * topology from ams_build_profile.h. The portable core must not depend on
 * the legacy application profile header, so only the estimator-local
 * values are reproduced here.
 *
 * The v2.6.27 default build profile is AMS_PROFILE_BENCH, whose estimator
 * default is PACK. Preserve that exact default for direct oracle parity.
 * Zephyr application profiles may explicitly select pack/segments later by
 * calling the existing configure APIs; they must not alter estimator math.
 */

#ifndef AMS_CORE_ESTIMATOR_CONFIG_H_
#define AMS_CORE_ESTIMATOR_CONFIG_H_

#define AMS_ESTIMATOR_TOPOLOGY_PACK     1
#define AMS_ESTIMATOR_TOPOLOGY_SEGMENTS 2

#ifndef AMS_ESTIMATOR_DEFAULT_TOPOLOGY
#define AMS_ESTIMATOR_DEFAULT_TOPOLOGY AMS_ESTIMATOR_TOPOLOGY_PACK
#endif

#if (AMS_ESTIMATOR_DEFAULT_TOPOLOGY != AMS_ESTIMATOR_TOPOLOGY_PACK) && \
    (AMS_ESTIMATOR_DEFAULT_TOPOLOGY != AMS_ESTIMATOR_TOPOLOGY_SEGMENTS)
#error "AMS_ESTIMATOR_DEFAULT_TOPOLOGY must be PACK or SEGMENTS"
#endif

#endif /* AMS_CORE_ESTIMATOR_CONFIG_H_ */
