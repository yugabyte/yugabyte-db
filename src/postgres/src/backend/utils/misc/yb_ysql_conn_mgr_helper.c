/*-------------------------------------------------------------------------
 *
 * yb_ysql_conn_mgr_helper.c
 * Utilities for Ysql Connection Manager/Yugabyte (Postgres layer) integration
 * that have to be defined on the PostgreSQL side.
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
 *	  src/backend/utils/misc/yb_ysql_conn_mgr_helper.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <ctype.h>
#include <float.h>
#include <sys/shm.h>
#include <unistd.h>

#include "access/htup_details.h"
#include "catalog/pg_authid.h"
#include "catalog/pg_type.h"
#include "catalog/pg_yb_role_profile.h"
#include "commands/dbcommands.h"
#include "libpq/libpq.h"
#include "libpq/libpq-be.h"
#include "libpq/pqformat.h"
#include "pg_yb_utils.h"
#include "utils/guc.h"
#include "utils/syscache.h"

#include "yb/yql/ysql_conn_mgr_wrapper/ysql_conn_mgr_stats.h"

#include "yb_ysql_conn_mgr_helper.h"

/* Max size of string that can be stored in shared memory */
#define SHMEM_MAX_STRING_LEN NAMEDATALEN

/* Max number of session parameters that can be stored in shared memory */
#define DEFAULT_SHMEM_ARR_LEN 10

/*
 * 0666 sets the access permissions of the memory segment (rwx).
 * IPC_CREAT tells the system to create a new memory segment for the shared
 * memory. IPC_EXCL tells system to throw error if the shared memory with given
 * key is already present.
 */
#define YB_CREATE_SHMEM_FLAG 0666 | IPC_EXCL | IPC_CREAT

bool yb_is_client_ysqlconnmgr = false;

enum SESSION_PARAMETER_UPDATE_RST
{
	SHMEM_RESIZE_NEEDED,
	ERROR_WHILE_STORING_SESSION_PARAMETER,
	NEED_TO_ADD_NEW_ELEMENT_IN_SHMEM_ARRAY,
	SUCCESSFULLY_UPDATED_SHMEM_VALUE
};

bool
YbIsClientYsqlConnMgr()
{
	return IsYugaByteEnabled() && yb_is_client_ysqlconnmgr;
}

int
attach_shmem(int shmem_id, char **shmem_ptr)
{
	*shmem_ptr = shmat(shmem_id, NULL, 0);
	if (*shmem_ptr == (void *) -1)
	{
		ereport(WARNING, (errmsg("Error at shmat for shared memory segment with "
							   "id '%d'. "
							   "%s",
							   shmem_id, strerror(errno))));
		return -1;
	}
	return 0;
}

int
detach_shmem(int shmem_id, void *shmem_ptr)
{
	if (shmdt(shmem_ptr) == -1)
	{
		ereport(WARNING, (errmsg("Error at shmdt for shared memory segment with "
							   "id '%d'. "
							   "%s",
							   shmem_id, strerror(errno))));
		return -1;
	}
	return 0;
}

struct ysql_conn_mgr_shmem_header
{
	/*
	 * Length of the array used to store the session parameter in the shared
	 * memory. It it the maximum number of session parameters that can be stored
	 * in the shared memory. If the number of changed session parameters exceede
	 * this value, then the shared memory will be resized to appropriate value
	 * (increasing the array len value).
	 */
	uint32_t session_parameter_array_len;

	Oid database;
	Oid user;
	bool is_superuser;
	char rolename[SHMEM_MAX_STRING_LEN];
};

struct shmem_session_parameter
{
	char name[SHMEM_MAX_STRING_LEN];
	char value[SHMEM_MAX_STRING_LEN];
};

/* 
 * List (linked list) of changed session parameters for the current transaction.
 */
struct changed_session_parameters_list
{
	/*
	 * TODO (janand) GH #18301 Use the index of the GUC list instead of string,
	 * to enhance the performance.
	 */
	char session_parameter_name[SHMEM_MAX_STRING_LEN];
	struct changed_session_parameters_list *next;
};

struct changed_session_parameters_list *yb_changed_session_parameters = NULL;

int yb_logical_client_shmem_key = -1;

int
get_shmem_size(const int array_len)
{
	return sizeof(struct ysql_conn_mgr_shmem_header) +
		   sizeof(struct shmem_session_parameter) * array_len;
}

void
YbCleanChangedSessionParameters()
{
	struct changed_session_parameters_list *temp_list;
	while (yb_changed_session_parameters != NULL)
	{
		temp_list = yb_changed_session_parameters->next;
		free(yb_changed_session_parameters);
		yb_changed_session_parameters = temp_list;
	}
}

void
YbAddToChangedSessionParametersList(const char *session_parameter_name)
{
	if (session_parameter_name == NULL || yb_logical_client_shmem_key == -1)
		return;

	/* 
	 * Length of `session_parameter_name` should be less than
	 * SHMEM_MAX_STRING_LEN.
	 */
	if (strlen(session_parameter_name) >= SHMEM_MAX_STRING_LEN)
	{
		/* TODO (janand) GH #18302 Handle this exception at the Ysql Conn Mgr
		 * side.
		 */
		ereport(WARNING, (errmsg("Unable to store session parameter '%s' in the "
							   "shared memory. Length of session parameter "
							   "(%d) exceeds the max limit(%d).",
							   session_parameter_name,
							   (int) (strlen(session_parameter_name)),
							   SHMEM_MAX_STRING_LEN)));
		return;
	}

	struct changed_session_parameters_list *temp_list;

	/* Check whether the session parameter is already present in the list */
	for (temp_list = yb_changed_session_parameters; temp_list != NULL;
		 temp_list = temp_list->next)
	{
		if (strncmp(temp_list->session_parameter_name, session_parameter_name,
					SHMEM_MAX_STRING_LEN) == 0)
			return;
	}

	temp_list = (struct changed_session_parameters_list *) malloc(
		sizeof(struct changed_session_parameters_list));
	strncpy(temp_list->session_parameter_name, session_parameter_name,
			SHMEM_MAX_STRING_LEN);
	temp_list->next = yb_changed_session_parameters;
	yb_changed_session_parameters = temp_list;
}

static int
change_array_len_in_shmem(const key_t shmem_id, const uint32_t new_array_size)
{
	char *shmem_ptr;
	if (attach_shmem(shmem_id, &shmem_ptr) < 0)
		return -1;

	memcpy(shmem_ptr + offsetof(struct ysql_conn_mgr_shmem_header,
								session_parameter_array_len),
		   &new_array_size, sizeof(new_array_size));

	if (detach_shmem(shmem_id, shmem_ptr) == -1)
		return -1;

	return 0;
}

static int
yb_shmem_resize(const key_t shmem_id, const long new_array_size)
{
	struct shmid_ds buf;
	int result = shmctl(shmem_id, IPC_STAT, &buf);
	if (result < 0)
	{
		ereport(WARNING, (errmsg("Error at shmctl for shared memory with key "
							   "'%d', while resizing the shared memory. %s",
							   shmem_id, strerror(errno))));
		return -1;
	}

	buf.shm_segsz = get_shmem_size(new_array_size);
	result = shmctl(shmem_id, IPC_SET, &buf);
	if (result < 0)
	{
		ereport(WARNING, (errmsg("Error at shmctl for shared memory with key "
							   "'%d', while resizing the shared memory. %s",
							   shmem_id, strerror(errno))));
		return -1;
	}

	if (change_array_len_in_shmem(shmem_id, new_array_size) != 0)
		return -1;

	ereport(DEBUG5,
			(errmsg("Resized shared memory size with id '%d'", shmem_id)));
	return 0;
}

static int32_t
check_resize_needed(const int shmem_id)
{
	/*
	 * TODO (janand) GH #18303 Compare the shared parameter names in present in
	 * shared memory and `shmem_parameter_list`, to find the accurate value.
	 */
	int max_length_needed = 0;
	int length_updates = 0;
	int length_shmem = 0;

	char *shmem_ptr;

	if (attach_shmem(shmem_id, &shmem_ptr) == -1)
		return -1;

	struct ysql_conn_mgr_shmem_header shmem_header;
	memcpy(&shmem_header, shmem_ptr, sizeof(struct ysql_conn_mgr_shmem_header));

	struct shmem_session_parameter *shmem_parameter_list =
		(struct shmem_session_parameter
			 *) (shmem_ptr + sizeof(struct ysql_conn_mgr_shmem_header));

	/* Find the used up length in the array  */
	for (int i = 0; (strncmp(shmem_parameter_list[i].name, "",
							 SHMEM_MAX_STRING_LEN) != 0) &&
					i < shmem_header.session_parameter_array_len;
		 i++, length_shmem++);

	/* Find the max number of elements needed in the array   */
	for (struct changed_session_parameters_list *temp_list =
			 yb_changed_session_parameters;
		 temp_list != NULL; temp_list = temp_list->next, length_updates++);

	max_length_needed = length_updates + length_shmem;

	unsigned long max_length_supported =
		shmem_header.session_parameter_array_len;

	if (detach_shmem(shmem_id, shmem_ptr) == -1)
		return -1;

	return (max_length_supported <= max_length_needed) ? 2 * max_length_needed :
														 0;
}

static int
resize_shmem_if_needed(const key_t shmem_id)
{
	/* TODO (janand) GH #18304 Add a java test for resizing the shared memory */
	int resize_needed = check_resize_needed(shmem_id);
	if (resize_needed == -1)
		return -1;

	if (resize_needed > 0 && (yb_shmem_resize(shmem_id, resize_needed) == -1))
	{
		ereport(WARNING, (errmsg("Error while resizing the shared memory segment "
							   "with key %d (%s).",
							   shmem_id, strerror(errno))));
		return -1;
	}

	return 0;
}

static int
update_session_parameter_value(
	struct shmem_session_parameter *shmem_parameter_list,
	const char *session_parameter_name, const uint32_t max_array_len,
	uint32_t *shmem_itr)
{
	for (*shmem_itr = 0; *shmem_itr < max_array_len;
		 *shmem_itr = *shmem_itr + 1)
	{
		if (strncmp(shmem_parameter_list[*shmem_itr].name,
					session_parameter_name, SHMEM_MAX_STRING_LEN) == 0)
		{
			/* TODO: Use GetConfigOptionByNum instead of GetConfigOptionByName. */
			char *value =
				GetConfigOptionByName(session_parameter_name, NULL, false);

			if (strlen(value) >= SHMEM_MAX_STRING_LEN)
			{
				ereport(WARNING, (errmsg("Value `%s` for session parameter `%s`, "
									   "exceeds the max allowable length",
									   value, session_parameter_name)));
				return ERROR_WHILE_STORING_SESSION_PARAMETER;
			}

			/* copy the client context to the shared memory */
			strncpy(shmem_parameter_list[*shmem_itr].value, value,
					SHMEM_MAX_STRING_LEN);
			return SUCCESSFULLY_UPDATED_SHMEM_VALUE;
		}

		if (strncmp(shmem_parameter_list[*shmem_itr].name, "",
					SHMEM_MAX_STRING_LEN) == 0)
			return NEED_TO_ADD_NEW_ELEMENT_IN_SHMEM_ARRAY;
	}

	ereport(WARNING, (errmsg("Unable to add the session parameter `%s` in the "
						   "shared memory "
						   ", needs to resize the array.",
						   session_parameter_name)));
	return SHMEM_RESIZE_NEEDED;
}

static int
add_session_parameter(struct shmem_session_parameter *shmem_parameter_list,
					  const char *session_parameter_name,
					  const uint32_t shmem_itr)
{
	Assert(strncmp(shmem_parameter_list[shmem_itr].name, "",
				   SHMEM_MAX_STRING_LEN) == 0);

	char *value = GetConfigOptionByName(session_parameter_name, NULL, false);
	if (strlen(value) >= SHMEM_MAX_STRING_LEN)
	{
		ereport(WARNING, (errmsg("Value `%s` for session parameter `%s`, exceeds "
							   "the max allowable length",
							   value, session_parameter_name)));
		return -1;
	}

	strncpy(shmem_parameter_list[shmem_itr].name, session_parameter_name,
			SHMEM_MAX_STRING_LEN);
	strncpy(shmem_parameter_list[shmem_itr].value, value, SHMEM_MAX_STRING_LEN);
	return 0;
}

static void
update_session_parameters(struct shmem_session_parameter *shmem_parameter_list,
						  const uint32_t max_shmem_array_size)
{
	for (struct changed_session_parameters_list *temp_list =
			 yb_changed_session_parameters;
		 temp_list != NULL; temp_list = temp_list->next)
	{
		char *session_parameter_name = temp_list->session_parameter_name;
		uint32_t shmem_itr = 0;
		int rc = update_session_parameter_value(shmem_parameter_list,
												session_parameter_name,
												max_shmem_array_size,
												&shmem_itr);

		switch (rc)
		{
			case SHMEM_RESIZE_NEEDED:
				// TODO (janand): Needs to be resized
				// Unexpected situation.
				Assert(false);
				break;

			case ERROR_WHILE_STORING_SESSION_PARAMETER:
				// Error while storing the session parameter
				ereport(WARNING, (errmsg("Unable to store the session parameter "
									   "%s",
									   session_parameter_name)));
				break;

			case NEED_TO_ADD_NEW_ELEMENT_IN_SHMEM_ARRAY:
				// Need to add a new element in the array.
				if (add_session_parameter(shmem_parameter_list,
										  session_parameter_name,
										  shmem_itr) < 0)
					ereport(WARNING, (errmsg("Unable to store the session "
										   "parameter %s",
										   session_parameter_name)));
				break;

			case SUCCESSFULLY_UPDATED_SHMEM_VALUE:
				// Session parameter is updated successfully.
				ereport(DEBUG5, (errmsg("Successfully stored the session "
										"parameter value %s",
										session_parameter_name)));
				break;

			default:
				// Invalid state
				Assert(false);
		}
	}
}

/*
 *						YbUpdateSharedMemory
 * Update the session parameter key-value parameter stored in session
 * parameters. Resize the shared memory block if needed.
 * NOTE: This function will only be called on `COMMIT`.
 */
void
YbUpdateSharedMemory()
{
	if (yb_logical_client_shmem_key == -1)
	{
		/* yb_changed_session_parameters can only be present if
		 * yb_logical_client_shmem_key is set */
		Assert(yb_changed_session_parameters == NULL);
		return;
	}

	int shmem_id = yb_logical_client_shmem_key;
	yb_logical_client_shmem_key = -1;

	if (yb_changed_session_parameters == NULL)
		return;

	if (resize_shmem_if_needed(shmem_id) < 0)
		return;

	char *shmem_ptr;
	if (attach_shmem(shmem_id, &shmem_ptr) < 0)
		return;

	struct ysql_conn_mgr_shmem_header shmem_header;
	memcpy(&shmem_header, shmem_ptr, sizeof(shmem_header));

	update_session_parameters(
		(struct shmem_session_parameter
			 *) (shmem_ptr + sizeof(struct ysql_conn_mgr_shmem_header)),
		shmem_header.session_parameter_array_len);

	detach_shmem(shmem_id, shmem_ptr);
}

int
yb_shmem_get(const Oid user, const char *user_name, bool is_superuser,
			 const Oid database)
{
	int shmem_id;
	char *shmem_ptr;

	if (strlen(user_name) >= SHMEM_MAX_STRING_LEN)
	{
		/*
		 * Use FATAL, to avoid any edge case of allocating any incorrect
		 * privilege.
		 */
		ereport(FATAL, ((errmsg("Length of the user name '%s' is exceeds the "
								"max supported length",
								user_name))));
	}

	shmem_id = shmget(IPC_PRIVATE, get_shmem_size(DEFAULT_SHMEM_ARR_LEN),
					  YB_CREATE_SHMEM_FLAG);
	if (shmem_id <= 0)
		return -1;

	/* Get the memory ptr */
	if (attach_shmem(shmem_id, &shmem_ptr) < 0)
		return -1;

	memcpy(shmem_ptr,
		   &(struct ysql_conn_mgr_shmem_header){.session_parameter_array_len =
													DEFAULT_SHMEM_ARR_LEN,
												.database = database,
												.user = user,
												.is_superuser = is_superuser},
		   sizeof(struct ysql_conn_mgr_shmem_header));

	strncpy(((struct ysql_conn_mgr_shmem_header *) shmem_ptr)->rolename,
			user_name, SHMEM_MAX_STRING_LEN);

	if (detach_shmem(shmem_id, shmem_ptr) == -1)
		return -1;

	return shmem_id;
}

void
SetSessionParameterFromSharedMemory(key_t client_shmem_key)
{
	yb_logical_client_shmem_key = client_shmem_key;

	char *shared_memory_ptr;
	if (attach_shmem(yb_logical_client_shmem_key, &shared_memory_ptr) < 0)
		return;

	struct ysql_conn_mgr_shmem_header shmem_header;
	memcpy(&shmem_header, shared_memory_ptr,
		   sizeof(struct ysql_conn_mgr_shmem_header));

	struct shmem_session_parameter *shmem_parameter_list =
		(struct shmem_session_parameter*) 
				(shared_memory_ptr + sizeof(struct ysql_conn_mgr_shmem_header));

	/*
	 * Due to "pool per user, db combination" setting the user context
	 * is not required.
	 */
#if YB_YSQL_CONN_MGR_POOL_MODE == POOL_PER_DB
	YbSetUserContext(shmem_header.user, shmem_header.is_superuser, shmem_header.rolename);
#endif

	int shmem_itr;
	for (shmem_itr = 0; shmem_itr < shmem_header.session_parameter_array_len;
		 shmem_itr++)
	{
		if (strncmp(shmem_parameter_list[shmem_itr].name, "",
					SHMEM_MAX_STRING_LEN) == 0)
			break;

		/*
		 * PGC_SUSET - Permissions equal to a super user using a SET statement.
		 * YSQL_CONN_MGR - Mark that the session parameter is loaded from
		 * 		the shared memory.
		 */
		(void) set_config_option(shmem_parameter_list[shmem_itr].name,
								 shmem_parameter_list[shmem_itr].value,
								 PGC_SUSET, YSQL_CONN_MGR, GUC_ACTION_SET,
								 true, 0, false);
	}

	/* Detach the shared memory */
	detach_shmem(client_shmem_key, shared_memory_ptr);
}

void
DeleteSharedMemory(int client_shmem_key)
{
	elog(DEBUG5, "Deleting the shared memory with key %d", client_shmem_key);

	/* Shared memory related to the client id will be removed */
	if (shmctl(client_shmem_key, IPC_RMID, NULL) == -1)
	{
		ereport(WARNING, (errmsg("Error at shmctl while trying to delete the "
							   "shared memory segment, %s",
							   strerror(errno))));
	}

	yb_logical_client_shmem_key = -1;
}

void
YbHandleSetSessionParam(int yb_client_id)
{
	/* This feature is only for Ysql Connection Manager */
	Assert(yb_is_client_ysqlconnmgr);

	/* 
	 * Create shared memory segment for the client is handled during the
	 * authentication.
	 */
	if (yb_client_id == 0)
		ereport(FATAL, (errmsg("Create shared memory for client is handled "
							   "only during the handling of authentication "
							   "passthrough request.")));

	/* Reset all the session parameters */
	ResetAllOptions();

	if (yb_client_id > 0)
		SetSessionParameterFromSharedMemory((key_t) yb_client_id);
	else
		DeleteSharedMemory((key_t) abs(yb_client_id));
}

/*
 * This function does checks that are mentioned in InitializeSessionUserId
 * function present in postinit.c and sets the is_superuser and roleid.
 *
 * Function InitializeSessionUserId can't be used here instead,
 * due to the need of a change in the signature of the function and handling of
 * failures.
 *
 * Checks done in this function:
 *  		1. Does the role exist.
 * 			2. Is the role permitted to login.
 */
static int8_t
SetLogicalClientUserDetailsIfValid(const char *rolename, bool *is_superuser,
						  Oid *roleid)
{
	HeapTuple	roleTup;
	Form_pg_authid rform;
	char	   *rname;

	/* TODO(janand) GH #19951 Do we need support for initializing via OID */
	Assert(rolename != NULL);

	roleTup = SearchSysCache1(AUTHNAME, PointerGetDatum(rolename));
	if (!HeapTupleIsValid(roleTup))
	{
		YbSendFatalForLogicalConnectionPacket();
		ereport(WARNING,
				(errcode(ERRCODE_INVALID_AUTHORIZATION_SPECIFICATION),
				 errmsg("role \"%s\" does not exist", rolename)));

		/* No need to call ReleaseSysCache here since the cache is invalid */
		return -1;
	}

	/* TODO(janand) GH #19951 Do we need support for initializing via OID */
	rform = (Form_pg_authid) GETSTRUCT(roleTup);
	*roleid = HeapTupleGetOid(roleTup);
	rname = NameStr(rform->rolname);
	*is_superuser = rform->rolsuper;

	/*
	 * Is role allowed to login at all?
	 */
	if (!rform->rolcanlogin)
	{
		YbSendFatalForLogicalConnectionPacket();
		ereport(WARNING,
				(errcode(ERRCODE_INVALID_AUTHORIZATION_SPECIFICATION),
				 errmsg("role \"%s\" is not permitted to log in", rname)));
		ReleaseSysCache(roleTup);
		return -1;
	}

	/*
	 * TODO(janand) GH #18886 Add support for "too many connections for role"
	 * error.
	 */
	ReleaseSysCache(roleTup);
	return 0;
}

void
YbCreateClientId(void)
{
	bool		is_superuser;
	Oid			user;
	Oid			database;

	/* This feature is only for Ysql Connection Manager */
	Assert(yb_is_client_ysqlconnmgr);

	if (SetLogicalClientUserDetailsIfValid(MyProcPort->user_name, &is_superuser, &user) < 0)
		return;

	database = get_database_oid(MyProcPort->database_name, true);
	if (database == InvalidOid)
	{
		YbSendFatalForLogicalConnectionPacket();
		ereport(WARNING,
				(errmsg("database \"%s\" does not exist",
						MyProcPort->database_name)));
		return;
	}

	/* Create a shared memory block for a client connection */
	int new_client_id = yb_shmem_get(user, MyProcPort->user_name, is_superuser, database);
	if (new_client_id > 0)
		ereport(WARNING, (errhint("shmkey=%d", new_client_id)));
	else
		ereport(FATAL, (errmsg("Unable to create the shared memory block")));
}

/*
 * `FATALFORLOGICALCONNECTION` packet informs the odyssey that the upcoming
 * WARNING packet should be treated as a FATAL packet.
 */
void
YbSendFatalForLogicalConnectionPacket()
{
	Assert(YbIsClientYsqlConnMgr());
	StringInfoData buf;

	CHECK_FOR_INTERRUPTS();

	pq_beginmessage(&buf, 'F');
	pq_endmessage(&buf);

	pq_flush();
	CHECK_FOR_INTERRUPTS();
}
