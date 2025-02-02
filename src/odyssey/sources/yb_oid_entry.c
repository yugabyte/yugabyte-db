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
#define MAX_DATABASES YSQL_CONN_MGR_MAX_POOLS
#define MAX_USERS YSQL_CONN_MGR_MAX_POOLS
#define YB_INVALID_OID_IN_PKT 0

yb_oid_entry_t database_entry_list[MAX_DATABASES];
yb_oid_entry_t user_entry_list[MAX_USERS];
od_atomic_u64_t database_count = 0;
od_atomic_u64_t user_count = 0;

pthread_rwlock_t database_rwlock = PTHREAD_RWLOCK_INITIALIZER;
pthread_rwlock_t user_rwlock = PTHREAD_RWLOCK_INITIALIZER;

static inline void set_oid_obj_name(yb_oid_entry_t *entry, const char *name)
{
	assert(entry != NULL);

	strcpy((char *)entry->name, name);
	entry->name_len = strlen(name);
}

static inline int add_oid_obj_entry(int obj_type, int oid, const char *name,
			       od_instance_t *instance)
{
	od_atomic_u64_t *oid_obj_count;
	uint64_t max_oid_obj_count;
	yb_oid_entry_t *oid_obj_entry_list;
	pthread_rwlock_t *lock;

	if (obj_type == YB_DATABASE) {
		oid_obj_count = &database_count;
		max_oid_obj_count = MAX_DATABASES;
		oid_obj_entry_list = database_entry_list;
		lock = &database_rwlock;
	} else {
		oid_obj_count = &user_count;
		max_oid_obj_count = MAX_USERS;
		oid_obj_entry_list = user_entry_list;
		lock = &user_rwlock;
	}

	pthread_rwlock_wrlock(lock);

	/* TODO(janand): GH#21437 Add tests for too many db pools */
	if (*oid_obj_count >= max_oid_obj_count) {
		pthread_rwlock_unlock(lock);
		return -1;
	}

	for (int i = 0; i < (int) *oid_obj_count; ++i) {
		if (oid_obj_entry_list[i].oid == oid) {
            pthread_rwlock_unlock(lock);
			assert(oid_obj_entry_list[i].oid != oid);
        }
	}

	const int newOidObjEntryIdx = od_atomic_u32_inc(oid_obj_count);
	set_oid_obj_name(oid_obj_entry_list + newOidObjEntryIdx, name);
	oid_obj_entry_list[newOidObjEntryIdx].oid = oid;
	oid_obj_entry_list[newOidObjEntryIdx].status = YB_OID_ACTIVE;
	pthread_rwlock_unlock(lock);
	if (obj_type == YB_DATABASE)
		od_debug(
			&instance->logger, "yb db oid", NULL, NULL,
			"Added the entry for database %s with oid %d in the global database_entry_list",
			(char *)oid_obj_entry_list[newOidObjEntryIdx].name,
			oid_obj_entry_list[newOidObjEntryIdx].oid);
	else if (obj_type == YB_USER)
		od_debug(
			&instance->logger, "yb user oid", NULL, NULL,
			"Added the entry for user %s with oid %d in the global user_entry_list",
			(char *)oid_obj_entry_list[newOidObjEntryIdx].name,
			oid_obj_entry_list[newOidObjEntryIdx].oid);
	return 0;
}

static inline int yb_add_or_update_oid_obj_entry(const int obj_type,
						const int yb_obj_oid,
					    const char *obj_name,
					    od_instance_t *instance)
{
	uint64_t oid_obj_count;
	yb_oid_entry_t *oid_obj_entry_list;
	pthread_rwlock_t *lock;

	if (obj_type == YB_DATABASE) {
		lock = &database_rwlock;
		pthread_rwlock_wrlock(lock);
		oid_obj_count = database_count;
		oid_obj_entry_list = database_entry_list;
	} else if (obj_type == YB_USER) {
		lock = &user_rwlock;
		pthread_rwlock_wrlock(lock);
		oid_obj_count = user_count;
		oid_obj_entry_list = user_entry_list;
	}

	/* Update the entry if found, and set it to active */
	for (uint64_t i = 0; i < oid_obj_count; ++i) {
		if (oid_obj_entry_list[i].oid == yb_obj_oid) {
			set_oid_obj_name(oid_obj_entry_list + i, obj_name);
			oid_obj_entry_list[i].status = YB_OID_ACTIVE;
			pthread_rwlock_unlock(lock);
			return 0;
		}
	}

	pthread_rwlock_unlock(lock);

	if (add_oid_obj_entry(obj_type, yb_obj_oid, obj_name, instance) < 0)
		return -1;

	return 0;
}

yb_oid_entry_t *yb_get_oid_obj_entry(const int obj_type, const int yb_obj_oid)
{
	assert(yb_obj_oid >= 0);

	yb_oid_entry_t *oid_obj_entry_list;
	od_atomic_u64_t oid_obj_count;
	pthread_rwlock_t *lock;

	if (obj_type == YB_DATABASE) {
		lock = &database_rwlock;
		pthread_rwlock_rdlock(lock);
		oid_obj_entry_list = database_entry_list;
		oid_obj_count = database_count;
	} else if (obj_type == YB_USER) {
		lock = &user_rwlock;
		pthread_rwlock_rdlock(lock);
		oid_obj_entry_list = user_entry_list;
		oid_obj_count = user_count;
	}

	/* control connection user/db are stored at start of the list */
	if (yb_obj_oid == YB_CTRL_CONN_OID) {
		pthread_rwlock_unlock(lock);
		return oid_obj_entry_list;
	}

	for (uint64_t i = 1; i < oid_obj_count; ++i) {
		if (oid_obj_entry_list[i].oid == yb_obj_oid) {
			pthread_rwlock_unlock(lock);
			return oid_obj_entry_list + i;
		}
	}
			

	pthread_rwlock_unlock(lock);
	return NULL;
}

void yb_oid_list_init(od_instance_t *instance)
{
	/* Add entry for the control connection */
	add_oid_obj_entry(YB_DATABASE, YB_CTRL_CONN_OID, "yugabyte", instance);
	add_oid_obj_entry(YB_USER, YB_CTRL_CONN_OID, "yugabyte", instance);
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

	rc = update_oid_entry_from_backend(obj_type, instance, server, entry);

	/*
	 * close the backend connection as we don't want to reuse machines in
	 * this pool if auth-backend is enabled.
	 */
	if (instance->config.yb_use_auth_backend)
		server->offline = true;
	od_router_detach(router, control_conn_client);
	od_router_unroute(router, control_conn_client);
	if (instance->config.yb_use_auth_backend && control_conn_client->io.io) {
		machine_close(control_conn_client->io.io);
		machine_io_free(control_conn_client->io.io);
	}
	od_client_free(control_conn_client);

	if (rc == -1)
		return -1;

	return OK_RESPONSE;

failed_to_acquire_control_connection:
	return NOT_OK_RESPONSE;
}

int yb_resolve_oid_status(const int obj_type, od_global_t *global, yb_oid_entry_t *entry,
			 od_server_t *server)
{
	if (server == NULL) {
		return update_oid_entry_via_ctrl_conn(obj_type, global, entry);
	} else {
		return update_oid_entry_from_backend(obj_type, global->instance, server, entry);
	}
}

/* different return status depending upon which of db or user entry is invalid */
int yb_is_route_invalid(void *route)
{
	pthread_rwlock_t *lock;

	lock = &database_rwlock;
	pthread_rwlock_rdlock(lock);
	if (((od_route_t *)route)->yb_database_entry->status == YB_OID_DROPPED) {
		pthread_rwlock_unlock(lock);
		return ROUTE_INVALID_DB_OID;
	}
	pthread_rwlock_unlock(lock);

	lock = &user_rwlock;
	pthread_rwlock_rdlock(lock);
	if (((od_route_t *)route)->yb_user_entry->status == YB_OID_DROPPED) {
		pthread_rwlock_unlock(lock);
		return ROUTE_INVALID_ROLE_OID;
	}
	pthread_rwlock_unlock(lock);

	return 0;
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
			     machine_msg_t *msg)
{
	char oid_type;
	uint32_t oid_val;
	yb_oid_entry_t *oid_obj_entry_list;
	int oid_obj_type;

	int rc = read_oid_pkt(server, msg, instance, &oid_type, &oid_val);
	if (rc == -1)
		return -1;

	if (oid_type == 'd') {
		oid_obj_entry_list = ((od_route_t *)server->route)->yb_database_entry;
		oid_obj_type = YB_DATABASE;
	} else if (oid_type == 'u') {
		oid_obj_entry_list = ((od_route_t *)server->route)->yb_user_entry;
		oid_obj_type = YB_USER;
	}

	assert(oid_obj_entry_list != NULL);

	/* database/user dropped just before sending oid packet */
	if (oid_val == YB_INVALID_OID_IN_PKT) {
		oid_obj_entry_list->status = YB_OID_DROPPED;
		return -1;
	}

	/* control connection */
	if (!oid_obj_entry_list->oid)
		return 0;

	if (oid_obj_entry_list->oid != (int)oid_val) {
		/* db/user has been recreated, mark original db/user as dropped */
		oid_obj_entry_list->status = YB_OID_DROPPED;
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

	/* Do nothing for a logical connection failure */
	if (oid_val == YB_INVALID_OID_IN_PKT)
		return 0;

	if (oid_type == 'd') {
		client->yb_db_oid = oid_val;
		return yb_add_or_update_oid_obj_entry(
			YB_DATABASE, oid_val, client->startup.database.value, instance);
	} else if (oid_type == 'u') {
		client->yb_user_oid = oid_val;
		return yb_add_or_update_oid_obj_entry(
			YB_USER, oid_val, client->startup.user.value, instance);
	}

	return 0;
}
