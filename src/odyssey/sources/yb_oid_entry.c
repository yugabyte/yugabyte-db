/*-------------------------------------------------------------------------
 *
 * yb_oid_entry.c
 * Utilities for Ysql Connection Manager/Yugabyte (Postgres layer) integration
 * that have to be defined on the Ysql Connection Manager (Odyssey) side.
 *
 * Copyright (c) YugaByteDB, Inc.
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
#include "yb_oid_entry.h"

/* TODO(janand): GH#21436 Use hash map instead of list */
#define MAX_DATABASES YSQL_CONN_MGR_MAX_POOLS
#define YB_INVALID_OID_IN_PKT 0

yb_db_entry_t database_entry_list[MAX_DATABASES];
od_atomic_u64_t database_count = 0;

static inline void set_db_name(yb_db_entry_t *db_entry, const char *db_name)
{
	assert(db_entry != NULL);

	strcpy((char *)db_entry->name, db_name);
	db_entry->name_len = strlen(db_name);
}

static inline int add_db_entry(int oid, const char *name,
			       od_instance_t *instance)
{
	/* TODO(janand): GH#21437 Add tests for too many db pools */
	if (database_count >= MAX_DATABASES)
		return -1;

	const int newDbEntryIdx = od_atomic_u32_inc(&database_count);
	if (newDbEntryIdx >= MAX_DATABASES)
		return -1;

	for (int i = 0; i < newDbEntryIdx; ++i) {
		assert(database_entry_list[i].oid != oid);
	}

	set_db_name(database_entry_list + newDbEntryIdx, name);
	database_entry_list[newDbEntryIdx].oid = oid;
	database_entry_list[newDbEntryIdx].status = YB_DB_ACTIVE;
	od_debug(
		&instance->logger, "yb db oid", NULL, NULL,
		"Added the entry for database %s with oid %d in the global database_entry_list",
		(char *)database_entry_list[newDbEntryIdx].name,
		database_entry_list[newDbEntryIdx].oid);
	return 0;
}

static inline int yb_add_or_update_db_entry(const int yb_db_oid,
					    const char *db_name,
					    od_instance_t *instance)
{
	/* Update the entry if found */
	for (uint64_t i = 0; i < database_count; ++i) {
		if (database_entry_list[i].oid == yb_db_oid) {
			set_db_name(database_entry_list + i, db_name);
			return 0;
		}
	}

	if (add_db_entry(yb_db_oid, db_name, instance) < 0)
		return -1;

	return 0;
}

yb_db_entry_t *yb_get_db_entry(const int yb_db_oid)
{
	assert(yb_db_oid >= 0);

	/* yb_db_oid = 0 repesents control connection */
	if (yb_db_oid == 0)
		return database_entry_list;

	for (uint64_t i = 1; i < database_count; ++i)
		if (database_entry_list[i].oid == yb_db_oid)
			return database_entry_list + i;

	return NULL;
}

void yb_db_list_init(od_instance_t *instance)
{
	/* Add entry for the control connection */
	add_db_entry(YB_CTRL_CONN_OID, "yugabyte", instance);
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

	/* db name */
	return pos;

error:
	return NULL;
}

static inline int update_db_entry_from_backend(od_instance_t *instance,
					       od_server_t *server,
					       yb_db_entry_t *entry)
{
	char query[240];
	sprintf(query,
		"SELECT datname AS db_name FROM pg_database WHERE oid IN (%d)",
		entry->oid);

	machine_msg_t *msg = od_query_do(server, "auth query", query, NULL);
	if (msg == NULL) {
		od_error(&instance->logger, "auth_query", NULL, server,
			 "auth query returned empty msg");
		return -1;
	}

	char *value = parse_single_col_data_row_pkt(msg);
	if (value == NULL) {
		/* i.e. db is dropped */
		entry->status = YB_DB_DROPPED;
	} else {
		set_db_name(entry, value);
	}

	machine_msg_free(msg);
	return 0;
}

static inline int update_db_entry_via_ctrl_conn(od_global_t *global,
						yb_db_entry_t *entry)
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
		od_client_free(control_conn_client);
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
		od_client_free(control_conn_client);
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
			od_client_free(control_conn_client);
			goto failed_to_acquire_control_connection;
		}
	}

	rc = update_db_entry_from_backend(instance, server, entry);

	/* detach and unroute */
	od_router_detach(router, control_conn_client);
	od_router_unroute(router, control_conn_client);
	od_client_free(control_conn_client);

	if (rc == -1)
		return -1;

	return OK_RESPONSE;

failed_to_acquire_control_connection:
	return NOT_OK_RESPONSE;
}

int yb_resolve_db_status(od_global_t *global, yb_db_entry_t *entry,
			 od_server_t *server)
{
	if (server == NULL) {
		return update_db_entry_via_ctrl_conn(global, entry);
	} else {
		return update_db_entry_from_backend(global->instance, server,
						    entry);
	}
}

bool yb_is_route_invalid(void *route)
{
	return ((od_route_t *)route)->yb_database_entry->status ==
	       YB_DB_DROPPED;
}

int read_oid_pkt(od_client_t *client, machine_msg_t *msg,
		 od_instance_t *instance, char *oid_type, uint32_t *oid_val)
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
			     machine_msg_t *msg, const char *db_name)
{
	char oid_type;
	uint32_t oid_val;

	int rc = read_oid_pkt(server, msg, instance, &oid_type, &oid_val);
	if (rc == -1)
		return -1;

	assert(((od_route_t *)server->route)->yb_database_entry != NULL);

	if (oid_val == YB_INVALID_OID_IN_PKT) {
		/* database is already declared invalid */
		if (yb_is_route_invalid(server->route))
			return 0;

		/* database dropped just before sending oid packet */
		return yb_resolve_db_status(
			instance,
			((od_route_t *)server->route)->yb_database_entry, NULL);
	}

	/* control connection */
	if (!((od_route_t *)server->route)->yb_database_entry->oid)
		return 0;

	if (((od_route_t *)server->route)->yb_database_entry->oid !=
	    (int)oid_val) {
		/* Database has been renamed or recreated */
		yb_resolve_db_status(
			instance,
			((od_route_t *)server->route)->yb_database_entry,
			server);
		return -1;
	}

	return 0;
}

int yb_handle_oid_pkt_client(od_instance_t *instance, od_client_t *client,
			     machine_msg_t *msg)
{
	char oid_type;
	uint32_t oid_val;

	if (read_oid_pkt(client, msg, instance, &oid_type, &oid_val) == -1)
		return -1;

	if (oid_type == 'd') {
		/* Do nothing for a logical connection failure */
		if (oid_val == YB_INVALID_OID_IN_PKT)
			return 0;

		client->yb_db_oid = oid_val;
		return yb_add_or_update_db_entry(
			oid_val, client->startup.database.value, instance);
	}

	return 0;
}
