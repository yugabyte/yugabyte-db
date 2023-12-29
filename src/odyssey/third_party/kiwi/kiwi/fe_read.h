#ifndef KIWI_FE_READ_H
#define KIWI_FE_READ_H

/*
 * kiwi.
 *
 * postgreSQL protocol interaction library.
 */

typedef struct kiwi_fe_error kiwi_fe_error_t;

struct kiwi_fe_error {
	char *severity;
	char *code;
	char *message;
	char *detail;
	char *hint;
};

KIWI_API static inline int kiwi_fe_read_ready(char *data, uint32_t size,
					      int *status)
{
	kiwi_header_t *header = (kiwi_header_t *)data;
	uint32_t len;
	int rc = kiwi_read(&len, &data, &size);
	if (kiwi_unlikely(rc != 0))
		return -1;
	if (kiwi_unlikely(header->type != KIWI_BE_READY_FOR_QUERY || len != 1))
		return -1;
	*status = *kiwi_header_data(header);
	return 0;
}

KIWI_API static inline int kiwi_fe_read_key(char *data, uint32_t size,
					    kiwi_key_t *key)
{
	kiwi_header_t *header = (kiwi_header_t *)data;
	uint32_t len;
	int rc = kiwi_read(&len, &data, &size);
	if (kiwi_unlikely(rc != 0))
		return -1;
	if (kiwi_unlikely(header->type != KIWI_BE_BACKEND_KEY_DATA || len != 8))
		return -1;
	uint32_t pos_size = len;
	char *pos = kiwi_header_data(header);
	rc = kiwi_read32(&key->key_pid, &pos, &pos_size);
	if (kiwi_unlikely(rc == -1))
		return -1;
	rc = kiwi_read32(&key->key, &pos, &pos_size);
	if (kiwi_unlikely(rc == -1))
		return -1;
	return 0;
}

KIWI_API static inline int yb_kiwi_fe_auth_packet_type(char *data, uint32_t size)
{

	kiwi_header_t *header = (kiwi_header_t *)data;
	uint32_t len;
	uint32_t type;
	int rc = kiwi_read(&len, &data, &size);
	if (kiwi_unlikely(rc != 0))
		return -1;
	if (kiwi_unlikely(header->type != KIWI_BE_AUTHENTICATION))
		return -1;
	uint32_t pos_size = len;
	char *pos = kiwi_header_data(header);
	rc = kiwi_read32(&type, &pos, &pos_size);
	if (kiwi_unlikely(rc == -1))
		return -1;
	return type;
}

KIWI_API static inline int kiwi_fe_read_auth(char *data, uint32_t size,
					     uint32_t *type, char salt[4],
					     char **auth_data,
					     size_t *auth_data_size)
{
	kiwi_header_t *header = (kiwi_header_t *)data;
	uint32_t len;
	int rc = kiwi_read(&len, &data, &size);
	if (kiwi_unlikely(rc != 0))
		return -1;
	if (kiwi_unlikely(header->type != KIWI_BE_AUTHENTICATION))
		return -1;
	uint32_t pos_size = len;
	char *pos = kiwi_header_data(header);
	rc = kiwi_read32(type, &pos, &pos_size);
	if (kiwi_unlikely(rc == -1))
		return -1;
	switch (*type) {
	/* AuthenticationOk */
	case 0:
		return 0;
	/* AuthenticationCleartextPassword */
	case 3:
		return 0;
	/* AuthenticationMD5Password */
	case 5:
		if (pos_size != 4)
			return -1;
		memcpy(salt, pos, 4);
		return 0;
	/* AuthenticationSASL */
	case 10:
		/* SCRAM-SHA-256 is the only implemented SASL mechanism in
			 * PostgreSQL, at the moment */
		if (strcmp(pos, "SCRAM-SHA-256") != 0)
			return -1;
		return 0;
	/* AuthenticationSASLContinue */
	case 11:
	/* AuthenticationSASLFinal */
	case 12:
		if (auth_data != NULL)
			*auth_data = pos;
		if (auth_data_size != NULL)
			*auth_data_size = pos_size;
		return 0;
	}
	/* unsupported */
	return -1;
}

KIWI_API static inline int
kiwi_fe_read_parameter(char *data, uint32_t size, char **name,
		       uint32_t *name_len, char **value, uint32_t *value_len)
{
	kiwi_header_t *header = (kiwi_header_t *)data;
	uint32_t len;
	int rc = kiwi_read(&len, &data, &size);
	if (kiwi_unlikely(rc != 0))
		return -1;
	if (kiwi_unlikely(header->type != KIWI_BE_PARAMETER_STATUS))
		return -1;
	uint32_t pos_size = len;
	char *pos = kiwi_header_data(header);
	/* name */
	*name = pos;
	rc = kiwi_readsz(&pos, &pos_size);
	if (kiwi_unlikely(rc == -1))
		return -1;
	*name_len = pos - *name;
	/* value */
	*value = pos;
	rc = kiwi_readsz(&pos, &pos_size);
	if (kiwi_unlikely(rc == -1))
		return -1;
	*value_len = pos - *value;
	return 0;
}

KIWI_API static inline int
kiwi_fe_read_error_or_notice(char *data, uint32_t size, kiwi_fe_error_t *error,
			     kiwi_be_type_t expected_packet_type)
{
	kiwi_header_t *header = (kiwi_header_t *)data;
	uint32_t len;
	int rc = kiwi_read(&len, &data, &size);
	if (kiwi_unlikely(rc != 0))
		return -1;
	if (kiwi_unlikely(header->type != expected_packet_type))
		return -1;
	memset(error, 0, sizeof(*error));
	uint32_t pos_size = len;
	char *pos = kiwi_header_data(header);
	for (;;) {
		char type;
		int rc;
		rc = kiwi_read8(&type, &pos, &pos_size);
		if (kiwi_unlikely(rc == -1))
			return -1;
		switch (type) {
		/* severity */
		case 'S':
			error->severity = pos;
			rc = kiwi_readsz(&pos, &pos_size);
			if (kiwi_unlikely(rc == -1))
				return -1;
			break;
		/* sqlstate */
		case 'C':
			error->code = pos;
			rc = kiwi_readsz(&pos, &pos_size);
			if (kiwi_unlikely(rc == -1))
				return -1;
			break;
		/* message */
		case 'M':
			error->message = pos;
			rc = kiwi_readsz(&pos, &pos_size);
			if (kiwi_unlikely(rc == -1))
				return -1;
			break;
		/* detail */
		case 'D':
			error->detail = pos;
			rc = kiwi_readsz(&pos, &pos_size);
			if (kiwi_unlikely(rc == -1))
				return -1;
			break;
		/* hint */
		case 'H':
			error->hint = pos;
			rc = kiwi_readsz(&pos, &pos_size);
			if (kiwi_unlikely(rc == -1))
				return -1;
			break;
		/* end */
		case 0:
			return 0;
		default:
			rc = kiwi_readsz(&pos, &pos_size);
			if (kiwi_unlikely(rc == -1))
				return -1;
			break;
		}
	}
	return 0;
}

KIWI_API static inline int kiwi_fe_read_error(char *data, uint32_t size,
					      kiwi_fe_error_t *error)
{
	return kiwi_fe_read_error_or_notice(data, size, error,
					    KIWI_BE_ERROR_RESPONSE);
}

KIWI_API static inline int kiwi_fe_read_notice(char *data, uint32_t size,
					       kiwi_fe_error_t *error)
{
	return kiwi_fe_read_error_or_notice(data, size, error,
					    KIWI_BE_NOTICE_RESPONSE);
}

#endif /* KIWI_FE_READ_H */
