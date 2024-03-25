#ifndef OID_CLEANUP_H
#define OID_CLEANUP_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sources/instance.h"

#include <kiwi.h>
#include <argp.h>

#define YB_CTRL_CONN_OID 0

enum yb_db_status { YB_DB_ACTIVE, YB_DB_DROPPED };

typedef struct {
	volatile int name_len;
	int oid;
	volatile int status;

	/* max db name len is 64 in yb */
	volatile char name[64];
} yb_db_entry_t;

extern int yb_handle_oid_pkt_server(od_instance_t *instance,
				    od_server_t *server, machine_msg_t *msg,
				    const char *db_name);

extern yb_db_entry_t *yb_get_db_entry(const int yb_db_oid);

extern void yb_db_list_init(od_instance_t *instance);

extern int yb_resolve_db_status(od_global_t *global, yb_db_entry_t *entry,
				od_server_t *server);

extern bool yb_is_route_invalid(void *route);

extern int yb_handle_oid_pkt_client(od_instance_t *instance,
				    od_client_t *client, machine_msg_t *msg);

#endif
