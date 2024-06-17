#ifndef KIWI_FE_WRITE_H
#define KIWI_FE_WRITE_H

/*
 * kiwi.
 *
 * postgreSQL protocol interaction library.
 */

typedef struct kiwi_fe_arg kiwi_fe_arg_t;

struct kiwi_fe_arg {
	char *name;
	int len;
};

KIWI_API static inline machine_msg_t *
kiwi_fe_write_startup_message(machine_msg_t *msg, int argc, kiwi_fe_arg_t *argv)
{
	int size = sizeof(uint32_t) + /* len */
		   sizeof(uint32_t) + /* version */
		   sizeof(uint8_t); /* last '\0' */
	int i = 0;
	for (; i < argc; i++)
		size += argv[i].len;
	int offset = 0;
	if (msg)
		offset = machine_msg_size(msg);
	msg = machine_msg_create_or_advance(msg, size);
	if (kiwi_unlikely(msg == NULL))
		return NULL;
	char *pos;
	pos = (char *)machine_msg_data(msg) + offset;
	/* len */
	kiwi_write32(&pos, size);
	/* version */
	kiwi_write32(&pos, 196608);
	/* arguments */
	for (i = 0; i < argc; i++)
		kiwi_write(&pos, argv[i].name, argv[i].len);
	/* eof */
	kiwi_write8(&pos, 0);
	return msg;
}

KIWI_API static inline machine_msg_t *
kiwi_fe_write_cancel(machine_msg_t *msg, uint32_t pid, uint32_t key)
{
	int size = sizeof(uint32_t) + /* len */
		   sizeof(uint32_t) + /* special */
		   sizeof(uint32_t) + /* pid */
		   sizeof(uint32_t); /* key */
	int offset = 0;
	if (msg)
		offset = machine_msg_size(msg);
	msg = machine_msg_create_or_advance(msg, size);
	if (kiwi_unlikely(msg == NULL))
		return NULL;
	char *pos;
	pos = (char *)machine_msg_data(msg) + offset;
	/* len */
	kiwi_write32(&pos, size);
	/* special */
	kiwi_write32(&pos, 80877102);
	/* pid */
	kiwi_write32(&pos, pid);
	/* key */
	kiwi_write32(&pos, key);
	return msg;
}

KIWI_API static inline machine_msg_t *
kiwi_fe_write_ssl_request(machine_msg_t *msg)
{
	int size = sizeof(uint32_t) + /* len */
		   sizeof(uint32_t); /* special */
	int offset = 0;
	if (msg)
		offset = machine_msg_size(msg);
	msg = machine_msg_create_or_advance(msg, size);
	if (kiwi_unlikely(msg == NULL))
		return NULL;
	char *pos;
	pos = (char *)machine_msg_data(msg) + offset;
	/* len */
	kiwi_write32(&pos, size);
	/* special */
	kiwi_write32(&pos, 80877103);
	return msg;
}

KIWI_API static inline machine_msg_t *
kiwi_fe_write_terminate(machine_msg_t *msg)
{
	int size = sizeof(kiwi_header_t);
	int offset = 0;
	if (msg)
		offset = machine_msg_size(msg);
	msg = machine_msg_create_or_advance(msg, size);
	if (kiwi_unlikely(msg == NULL))
		return NULL;
	char *pos;
	pos = (char *)machine_msg_data(msg) + offset;
	kiwi_write8(&pos, KIWI_FE_TERMINATE);
	kiwi_write32(&pos, sizeof(uint32_t));
	return msg;
}

KIWI_API static inline machine_msg_t *
kiwi_fe_write_password(machine_msg_t *msg, char *password, int len)
{
	int size = sizeof(kiwi_header_t) + len;
	int offset = 0;
	if (msg)
		offset = machine_msg_size(msg);
	msg = machine_msg_create_or_advance(msg, size);
	if (kiwi_unlikely(msg == NULL))
		return NULL;

	assert(password != NULL);
	char *pos;
	pos = (char *)machine_msg_data(msg) + offset;
	kiwi_write8(&pos, KIWI_FE_PASSWORD_MESSAGE);
	kiwi_write32(&pos, sizeof(uint32_t) + len);
	kiwi_write(&pos, password, len);
	return msg;
}

KIWI_API static inline machine_msg_t *kiwi_fe_write_query(machine_msg_t *msg,
							  char *query, int len)
{
	int size = sizeof(kiwi_header_t) + len;
	int offset = 0;
	if (msg)
		offset = machine_msg_size(msg);
	msg = machine_msg_create_or_advance(msg, size);
	if (kiwi_unlikely(msg == NULL))
		return NULL;
	char *pos;
	pos = (char *)machine_msg_data(msg) + offset;
	kiwi_write8(&pos, KIWI_FE_QUERY);
	kiwi_write32(&pos, sizeof(uint32_t) + len);
	kiwi_write(&pos, query, len);
	return msg;
}

KIWI_API static inline machine_msg_t *
kiwi_fe_write_parse_description(machine_msg_t *msg, char *operator_name,
				int operator_len, char *description,
				int description_len)
{
	size_t payload_size = operator_len + description_len;

	uint32_t size = sizeof(kiwi_header_t) + payload_size;
	int offset = 0;
	if (msg)
		offset = machine_msg_size(msg);
	msg = machine_msg_create_or_advance(msg, size);
	if (kiwi_unlikely(msg == NULL))
		return NULL;
	char *pos;
	pos = (char *)machine_msg_data(msg) + offset;
	kiwi_write8(&pos, KIWI_FE_PARSE);
	kiwi_write32(&pos, sizeof(uint32_t) + payload_size);
	kiwi_write(&pos, operator_name, operator_len);
	kiwi_write(&pos, description, description_len);

	return msg;
}

KIWI_API static inline machine_msg_t *kiwi_fe_write_close(machine_msg_t *msg,
							  char type,
							  char *operator_name,
							  int operator_len)
{
	size_t payload_size = sizeof(char) + operator_len;
	uint32_t size = sizeof(kiwi_header_t) + payload_size;
	int offset = 0;
	if (msg)
		offset = machine_msg_size(msg);
	msg = machine_msg_create_or_advance(msg, size);
	if (kiwi_unlikely(msg == NULL))
		return NULL;
	char *pos;
	pos = (char *)machine_msg_data(msg) + offset;
	kiwi_write8(&pos, KIWI_FE_CLOSE);
	kiwi_write32(&pos, sizeof(uint32_t) + payload_size);
	kiwi_write8(&pos, type);
	kiwi_write(&pos, operator_name, operator_len);
	return msg;
}

KIWI_API static inline machine_msg_t *
kiwi_fe_write_parse(machine_msg_t *msg, char *operator_name, int operator_len,
		    char *query, int query_len, uint16_t typec, int *typev)
{
	size_t payload_size = operator_len + query_len + sizeof(uint16_t) +
			      typec * sizeof(uint32_t);
	uint32_t size = sizeof(kiwi_header_t) + payload_size;
	int offset = 0;
	if (msg)
		offset = machine_msg_size(msg);
	msg = machine_msg_create_or_advance(msg, size);
	if (kiwi_unlikely(msg == NULL))
		return NULL;
	char *pos;
	pos = (char *)machine_msg_data(msg) + offset;
	kiwi_write8(&pos, KIWI_FE_PARSE);
	kiwi_write32(&pos, sizeof(uint32_t) + payload_size);
	kiwi_write(&pos, operator_name, operator_len);
	kiwi_write(&pos, query, query_len);
	kiwi_write16(&pos, typec);
	int i = 0;
	for (; i < typec; i++)
		kiwi_write32(&pos, typev[i]);
	return msg;
}

KIWI_API static inline machine_msg_t *
kiwi_fe_write_bind(machine_msg_t *msg, char *portal_name, int portal_len,
		   char *operator_name, int operator_len, int argc_call_types,
		   int call_types[], int argc_result_types, int result_types[],
		   int argc, int *argv_len, char **argv)
{
	size_t payload_size =
		portal_len + operator_len +
		sizeof(uint16_t) + /* argc_call_types */
		sizeof(uint16_t) * argc_call_types + /* call_types */
		sizeof(uint16_t) + /* argc_result_types */
		sizeof(uint16_t) * argc_result_types + /* result_types */
		sizeof(uint16_t); /* argc */
	int i = 0;
	for (; i < argc; i++) {
		payload_size += sizeof(uint32_t);
		if (argv_len[i] == -1)
			continue;
		payload_size += argv_len[i];
	}
	int size = sizeof(kiwi_header_t) + payload_size;
	int offset = 0;
	if (msg)
		offset = machine_msg_size(msg);
	msg = machine_msg_create_or_advance(msg, size);
	if (kiwi_unlikely(msg == NULL))
		return NULL;
	char *pos;
	pos = (char *)machine_msg_data(msg) + offset;

	kiwi_write8(&pos, KIWI_FE_BIND);
	kiwi_write32(&pos, sizeof(uint32_t) + payload_size);
	kiwi_write(&pos, portal_name, portal_len);
	kiwi_write(&pos, operator_name, operator_len);
	kiwi_write16(&pos, argc_call_types);
	for (i = 0; i < argc_call_types; i++)
		kiwi_write16(&pos, call_types[i]);
	kiwi_write16(&pos, argc);
	for (i = 0; i < argc; i++) {
		kiwi_write32(&pos, argv_len[i]);
		if (argv_len[i] == -1)
			continue;
		kiwi_write(&pos, argv[i], argv_len[i]);
	}
	kiwi_write16(&pos, argc_result_types);
	for (i = 0; i < argc_result_types; i++)
		kiwi_write16(&pos, result_types[i]);
	return msg;
}

KIWI_API static inline machine_msg_t *kiwi_fe_write_describe(machine_msg_t *msg,
							     uint8_t type,
							     char *name,
							     int name_len)
{
	int size = sizeof(kiwi_header_t) + sizeof(type) + name_len;
	int offset = 0;
	if (msg)
		offset = machine_msg_size(msg);
	msg = machine_msg_create_or_advance(msg, size);
	if (kiwi_unlikely(msg == NULL))
		return NULL;
	char *pos;
	pos = (char *)machine_msg_data(msg) + offset;
	kiwi_write8(&pos, KIWI_FE_DESCRIBE);
	kiwi_write32(&pos, sizeof(uint32_t) + sizeof(type) + name_len);
	kiwi_write8(&pos, type);
	kiwi_write(&pos, name, name_len);
	return msg;
}

KIWI_API static inline machine_msg_t *kiwi_fe_write_execute(machine_msg_t *msg,
							    char *portal,
							    int portal_len,
							    uint32_t limit)
{
	int size = sizeof(kiwi_header_t) + portal_len + sizeof(limit);
	int offset = 0;
	if (msg)
		offset = machine_msg_size(msg);
	msg = machine_msg_create_or_advance(msg, size);
	if (kiwi_unlikely(msg == NULL))
		return NULL;
	char *pos;
	pos = (char *)machine_msg_data(msg) + offset;
	kiwi_write8(&pos, KIWI_FE_EXECUTE);
	kiwi_write32(&pos, sizeof(uint32_t) + portal_len + sizeof(limit));
	kiwi_write(&pos, portal, portal_len);
	kiwi_write32(&pos, limit);
	return msg;
}

KIWI_API static inline machine_msg_t *kiwi_fe_write_sync(machine_msg_t *msg)
{
	int size = sizeof(kiwi_header_t);
	int offset = 0;
	if (msg)
		offset = machine_msg_size(msg);
	msg = machine_msg_create_or_advance(msg, size);
	if (kiwi_unlikely(msg == NULL))
		return NULL;
	char *pos;
	pos = (char *)machine_msg_data(msg) + offset;
	kiwi_write8(&pos, KIWI_FE_SYNC);
	kiwi_write32(&pos, sizeof(uint32_t));
	return msg;
}

// support for mulit-param stmts
KIWI_API static inline machine_msg_t *
kiwi_fe_write_prep_stmt(machine_msg_t *msg, char *query, char *param)
{
	msg = kiwi_fe_write_parse(msg, "", 1, query, strlen(query) + 1, 0,
				  NULL);
	msg = kiwi_fe_write_bind(msg, "", 1, "", 1, 0, NULL, 0, NULL, 1,
				 (int[]){ strlen(param) }, (char *[]){ param });
	msg = kiwi_fe_write_describe(msg, 'P', "", 1);
	msg = kiwi_fe_write_execute(msg, "", 1, 0);
	msg = kiwi_fe_write_sync(msg);
	return msg;
}

/*
 * yb_logical_conn_type enum identifies type of connection between client
 * and ysql conn mgr.
 * 
 * Below are different values yb_logical_conn_type enum can have:
 * 
 * YB_LOGICAL_ENCRYPTED_CONN denotes SSL encrypted, TCP/IP Connection 
 * between client & ysql conn mgr
 * 
 * YB_LOGICAL_UNENCRYPTED_CONN denotes no SSL, TCP/IP Connection between
 * client & ysql conn mgr
 * 
 * YB_LOGICAL_USD_CONN denotes Unix Socket Connection between client &
 * ysql conn mgr which is currently not supported.
 * TODO(mkumar) GH #20048 Support for unix socket connectivity b/w client and
 * odyssey.
 */
typedef enum {
	YB_LOGICAL_ENCRYPTED_CONN = 'E',
	YB_LOGICAL_UNENCRYPTED_CONN = 'U',
	YB_LOGICAL_USD_CONN = 'L'
} yb_logical_conn_type;

KIWI_API static inline machine_msg_t *
kiwi_fe_write_authentication(machine_msg_t *msg, char *username, char *database,
			     char *peer, yb_logical_conn_type logical_conn_type )
{
	int size = sizeof(kiwi_header_t) +
		   sizeof(char) * (strlen(username) + strlen(database) +
				   strlen(peer) + 3) + sizeof(yb_logical_conn_type);

	int offset = 0;
	if (msg)
		offset = machine_msg_size(msg);
	msg = machine_msg_create_or_advance(msg, size);
	if (kiwi_unlikely(msg == NULL))
		return NULL;
	char *pos;
	pos = (char *)machine_msg_data(msg) + offset;
	kiwi_write8(&pos, KIWI_FE_AUTH);
	kiwi_write32(&pos, size - sizeof(uint8_t));
	kiwi_write(&pos, username, strlen(username) + 1); // username
	kiwi_write(&pos, database, strlen(database) + 1); // database
	kiwi_write(&pos, peer, strlen(peer) + 1); // host
	kiwi_write8(&pos, logical_conn_type); // conn type
	return msg;
}

KIWI_API static inline machine_msg_t *
kiwi_fe_write_set_client_id(machine_msg_t *msg, int arg)
{
	int size = sizeof(kiwi_header_t) + sizeof(uint32_t);
	int offset = 0;
	if (msg)
		offset = machine_msg_size(msg);
	msg = machine_msg_create_or_advance(msg, size);
	if (kiwi_unlikely(msg == NULL))
		return NULL;
	char *pos;
	pos = (char *)machine_msg_data(msg) + offset;
	kiwi_write8(&pos, KIWI_FE_SET_SESSION_PARAMETER);
	kiwi_write32(&pos, size - sizeof(uint8_t));
	kiwi_write32(&pos, arg); // client_id

	return msg;
}

KIWI_API static inline machine_msg_t *
kiwi_fe_write_authentication_sasl_initial(machine_msg_t *msg, char *mechanism,
					  char *initial_response,
					  int initial_response_len)
{
	int mechanism_len = strlen(mechanism);
	int size = sizeof(kiwi_header_t) + mechanism_len + sizeof(uint8_t) +
		   sizeof(initial_response_len) + initial_response_len;

	int offset = 0;
	if (msg)
		offset = machine_msg_size(msg);
	msg = machine_msg_create_or_advance(msg, size);
	if (kiwi_unlikely(msg == NULL))
		return NULL;
	char *pos;
	pos = (char *)machine_msg_data(msg) + offset;

	kiwi_write8(&pos, KIWI_FE_PASSWORD_MESSAGE);
	kiwi_write32(&pos, size - sizeof(uint8_t));
	kiwi_write(&pos, mechanism, mechanism_len);
	kiwi_write8(&pos, 0); /* write mechanism as a string */
	kiwi_write32(&pos, initial_response_len);
	kiwi_write(&pos, initial_response, initial_response_len);

	return msg;
}

KIWI_API static inline machine_msg_t *kiwi_fe_write_authentication_scram_final(
	machine_msg_t *msg, char *final_message, int final_message_len)
{
	int size = sizeof(kiwi_header_t) + final_message_len;

	int offset = 0;
	if (msg)
		offset = machine_msg_size(msg);
	msg = machine_msg_create_or_advance(msg, size);
	if (kiwi_unlikely(msg == NULL))
		return NULL;
	char *pos;
	pos = (char *)machine_msg_data(msg) + offset;

	kiwi_write8(&pos, KIWI_FE_PASSWORD_MESSAGE);
	kiwi_write32(&pos, size - sizeof(uint8_t));
	kiwi_write(&pos, final_message, final_message_len);

	return msg;
}

KIWI_API static inline machine_msg_t *kiwi_fe_copy_msg(machine_msg_t *msg,
						       char *data, int sizes)
{
	int size = sizes;
	int offset = 0;
	msg = machine_msg_create_or_advance(msg, size);
	if (kiwi_unlikely(msg == NULL))
		return NULL;
	char *pos;
	pos = (char *)machine_msg_data(msg) + offset;
	kiwi_write(&pos, data, sizes);
	return msg;
}

#endif /* KIWI_FE_WRITE_H */

