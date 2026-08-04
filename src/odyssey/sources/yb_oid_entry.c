/*-------------------------------------------------------------------------
 *
 * yb_oid_entry.c
 * Utilities for Ysql Connection Manager/Yugabyte (Postgres layer) integration
 * that have to be defined on the Ysql Connection Manager (Odyssey) side.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License.  You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 * IDENTIFICATION
 *	  sources/yb_oid_entry.c
 *
 *-------------------------------------------------------------------------
 */

#include <kiwi.h>
#include <machinarium.h>
#include <odyssey.h>
#include <pthread.h>
#include "yb_oid_entry.h"

/* TODO(janand): GH#21436 Use hash map instead of list */
#define YB_INVALID_OID_IN_PKT 0

static inline void set_oid_obj_name(yb_oid_entry_t *entry, const char *name)
{
	assert(entry != NULL);

	strcpy((char *)entry->name, name);
	entry->name_len = strlen(name);
}

static inline char *parse_single_col_data_row_pkt(machine_msg_t *msg)
{
	char *pos = (char *)machine_msg_data(msg) + 1;
	uint32_t pos_size = machine_msg_size(msg) - 1;

	/* size */
	uint32_t size;
	int rc;
	rc = kiwi_read32(&size, &pos, &pos_size);
	if (kiwi_unlikely(rc == -1))
		goto error;
	/* count */
	uint16_t count;
	rc = kiwi_read16(&count, &pos, &pos_size);

	if (kiwi_unlikely(rc == -1))
		goto error;

	if (count != 1)
		goto error;

	/* (not used) */
	uint32_t lag_len;
	rc = kiwi_read32(&lag_len, &pos, &pos_size);
	if (kiwi_unlikely(rc == -1)) {
		goto error;
	}

	/* user/db name */
	return pos;

error:
	return NULL;
}

static inline int update_oid_entry_from_backend(const int obj_type,
						   od_instance_t *instance,
					       od_server_t *server,
					       yb_oid_entry_t *entry)
{
	char query[240];
	if (obj_type == YB_DATABASE)
		sprintf(query,
			"SELECT datname AS db_name FROM pg_database WHERE oid IN (%d)",
			entry->oid);
	else if (obj_type == YB_USER)
		sprintf(query,
			"SELECT rolname AS user_name FROM pg_roles WHERE oid IN (%d)",
			entry->oid);

	machine_msg_t *msg = od_query_do(server, "auth query", query, NULL);
	if (msg == NULL) {
		/* od_query_do handles if msg is NULL due to errors */
		entry->status = YB_OID_DROPPED;
		return 0;
	}

	char *value = parse_single_col_data_row_pkt(msg);
	if (value == NULL) {
		/* i.e. user/db is dropped */
		entry->status = YB_OID_DROPPED;
	} else {
		set_oid_obj_name(entry, value);
	}

	machine_msg_free(msg);
	return 0;
}

static inline int update_oid_entry_via_ctrl_conn(const int obj_type, 
						od_global_t *global,
						yb_oid_entry_t *entry)
{
	od_instance_t *instance = global->instance;
	od_router_t *router = global->router;

	/* internal client */
	od_client_t *control_conn_client;
	control_conn_client =
		od_client_allocate_internal(global, "control connection");
	if (control_conn_client == NULL) {
		od_error(
			&instance->logger, "control connection",
			control_conn_client, NULL,
			"failed to allocate internal client for the control connection");
		goto failed_to_acquire_control_connection;
	}

#ifndef YB_GUC_SUPPORT_VIA_SHMEM
	yb_kiwi_var_set(&control_conn_client->startup.user,
			"control_connection_user", 24);

	yb_kiwi_var_set(&control_conn_client->startup.database,
			"control_connection_db", 22);
#else
	/* set control connection route user and database */
	kiwi_var_set(&control_conn_client->startup.user, KIWI_VAR_UNDEF,
		     "control_connection_user", 24);
	kiwi_var_set(&control_conn_client->startup.database, KIWI_VAR_UNDEF,
		     "control_connection_db", 22);
#endif

	/* route */
	od_router_status_t status;
	status = od_router_route(router, control_conn_client);
	if (status != OD_ROUTER_OK) {
		od_error(
			&instance->logger, "control connection query",
			control_conn_client, NULL,
			"failed to route internal client for control connection: %s",
			od_router_status_to_str(status));
		od_client_free_extended(control_conn_client);
		goto failed_to_acquire_control_connection;
	}

	/* attach */
	status = od_router_attach(router, control_conn_client, false,
				  control_conn_client);
	if (status != OD_ROUTER_OK) {
		od_error(
			&instance->logger, "control connection",
			control_conn_client, NULL,
			"failed to attach internal client for control connection to route: %s",
			od_router_status_to_str(status));
		od_router_unroute(router, control_conn_client);
		od_client_free_extended(control_conn_client);
		goto failed_to_acquire_control_connection;
	}

	od_server_t *server;
	server = control_conn_client->server;

	od_debug(&instance->logger, "control connection", control_conn_client,
		 server, "attached to server %s%.*s", server->id.id_prefix,
		 (int)sizeof(server->id.id), server->id.id);

	/* connect to server, if necessary */
	int rc;
	if (server->io.io == NULL) {
		rc = od_backend_connect(server, "control connection", NULL,
					control_conn_client);
		if (rc == NOT_OK_RESPONSE) {
			od_error(&instance->logger, "control connection",
				 control_conn_client, server,
				 "failed to acquire backend connection: %s",
				 od_io_error(&server->io));
			od_router_close(router, control_conn_client);
			od_router_unroute(router, control_conn_client);
			od_client_free_extended(control_conn_client);
			goto failed_to_acquire_control_connection;
		}
	}

	rc = update_oid_entry_from_backend(obj_type, instance, server, entry);

	/*
	 * close the backend connection as we don't want to reuse machines in
	 * this pool if auth-backend is enabled.
	 */
	if (instance->config.yb_use_auth_backend)
		server->offline = true;
	od_router_detach(router, control_conn_client);
	od_router_unroute(router, control_conn_client);
	od_client_free_extended(control_conn_client);

	if (rc == -1)
		return -1;

	return OK_RESPONSE;

failed_to_acquire_control_connection:
	return NOT_OK_RESPONSE;
}

/*
 * Currently below code never executes. And it's dependent upon yb_oid_entry_t which is no longer
 * been used (DB-15614). Make sure to update the below code before using it according to current
 * implementation of handling OIDs.
*/
int yb_resolve_oid_status(const int obj_type, od_global_t *global, yb_oid_entry_t *entry,
			 od_server_t *server)
{
	if (server == NULL) {
		return update_oid_entry_via_ctrl_conn(obj_type, global, entry);
	} else {
		return update_oid_entry_from_backend(obj_type, global->instance, server, entry);
	}
}

/* Return the status of the route whether it's valid or not */
int yb_is_route_invalid(void *route)
{
	return ((od_route_t *)route)->status == YB_ROUTE_INACTIVE ? YB_ROUTE_INVALID : 0;
}

int read_oid_pkt(machine_msg_t *msg, od_instance_t *instance, char *oid_type,
		 uint32_t *oid_val)
{
	char *pos = (char *)machine_msg_data(msg) + 1;
	uint32_t pos_size = machine_msg_size(msg) - 1;

	/* size */
	uint32_t size;
	int rc;
	rc = kiwi_read32(&size, &pos, &pos_size);
	if (kiwi_unlikely(rc == -1))
		return -1;

	/* Oid type */
	rc = kiwi_read8(oid_type, &pos, &pos_size);
	if (rc == -1)
		return -1;

	/* Oid val */
	rc = kiwi_read32(oid_val, &pos, &pos_size);
	if (rc == -1)
		return -1;

	return 0;
}

int yb_handle_oid_pkt_server(od_instance_t *instance, od_server_t *server,
			     machine_msg_t *msg)
{
	char oid_type;
	uint32_t oid_val;
	int route_oid;
	int db_oid = -1;
	int user_oid = -1;

	int rc = read_oid_pkt(msg, instance, &oid_type, &oid_val);
	if (rc == -1)
		return -1;

	if (oid_type == 'd') {
		route_oid = ((od_route_t *)server->route)->id.yb_db_oid;
		db_oid = route_oid;
	} else if (oid_type == 'u') {
		route_oid = ((od_route_t *)server->route)->id.yb_user_oid;
		user_oid = route_oid;
	}

	/* database/user dropped just before sending oid packet */
	if (oid_val == YB_INVALID_OID_IN_PKT) {
		yb_mark_routes_inactive(server->global->router, db_oid, user_oid);
		return -1;
	}

	/* control connection */
	if (!route_oid)
		return 0;

	if (route_oid != (int)oid_val) {
		/* db/user has been recreated, mark original db/user as dropped */
		yb_mark_routes_inactive(server->global->router, db_oid, user_oid);
		return -1;
	}

	return 0;
}

int yb_handle_oid_pkt_client(od_instance_t *instance, od_client_t *client,
			     machine_msg_t *msg)
{
	char oid_type;
	uint32_t oid_val;

	if (read_oid_pkt(msg, instance, &oid_type, &oid_val) == -1)
		return -1;

	/* Do nothing for a logical connection failure */
	if (oid_val == YB_INVALID_OID_IN_PKT)
		return 0;

	if (oid_type == 'd') {
		client->yb_db_oid = oid_val;
	} else if (oid_type == 'u') {
		client->yb_user_oid = oid_val;
	}

	return 0;
}
