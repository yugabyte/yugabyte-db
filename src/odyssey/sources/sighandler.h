#ifndef OD_SIGNAL_HANDLER
#define OD_SIGNAL_HANDLER

#include <kiwi.h>
#include <machinarium.h>
#include <odyssey.h>

void od_system_signal_handler(void *arg);

void od_system_shutdown(od_system_t *system, od_instance_t *instance);

#endif /* OD_SIGNAL_HANDLER */
