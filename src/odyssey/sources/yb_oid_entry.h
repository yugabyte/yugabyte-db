#ifndef OID_CLEANUP_H
#define OID_CLEANUP_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sources/instance.h"

#include <kiwi.h>
#include <argp.h>

#define YB_CTRL_CONN_OID 0
#define ROUTE_INVALID_DB_OID 1
#define ROUTE_INVALID_ROLE_OID 2
#define YB_ROUTE_INVALID 1
#define YB_CTRL_CONN_DB_NAME "yugabyte"
#define YB_CTRL_CONN_USER_NAME "yugabyte"

enum yb_oid_status { YB_OID_ACTIVE, YB_OID_DROPPED };
enum yb_oid_object { YB_USER, YB_DATABASE };

typedef struct {
	volatile int name_len;
	int oid;
	volatile int status;

	/* max user/db name len is 64 in yb */
	volatile char name[64];
} yb_oid_entry_t;

extern int yb_handle_oid_pkt_server(od_instance_t *instance,
				    od_server_t *server, machine_msg_t *msg);

extern int yb_resolve_oid_status(const int obj_type, od_global_t *global, yb_oid_entry_t *entry,
				od_server_t *server);

extern int yb_is_route_invalid(void *route);

extern int yb_handle_oid_pkt_client(od_instance_t *instance,
				    od_client_t *client, machine_msg_t *msg);

#endif
