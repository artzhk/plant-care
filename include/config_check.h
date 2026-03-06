#ifndef CONFIG_CHECK_H
#define CONFIG_CHECK_H

#if defined(NODE_MASTER) || defined(NODE_SENSOR)
#if defined(NODE_SENSOR)
#if !defined(MASTER_IP)
#error "MASTER_IP must be defined in the compile flags"
#endif
#if !defined(HA_IP)
#error "HA_IP must be defined in the compile flags"
#endif
#endif

#if defined(NODE_MASTER) && defined(NODE_SENSOR) && defined(DISPLAY)
#error "Define only one configuration: NODE_MASTER or NODE_SENSOR or DISPLAY"
#endif
#endif

#if defined(DISPLAY)
#if (defined(NODE_MASTER) || defined(NODE_SENSOR)) && defined(DISPLAY) != 1
#error "Define only one configuration: NODE_MASTER or NODE_SENSOR or DISPLAY"
#endif
#endif

#endif
