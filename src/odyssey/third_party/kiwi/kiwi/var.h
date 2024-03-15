#ifndef KIWI_VAR_H
#define KIWI_VAR_H

/*
 * kiwi.
 *
 * postgreSQL protocol interaction library.
 */

#define KIWI_MAX_VAR_SIZE 128
#define KIWI_MAX_NAME_SIZE 64

typedef struct kiwi_var kiwi_var_t;
typedef struct kiwi_vars kiwi_vars_t;

typedef enum {
	KIWI_VAR_CLIENT_ENCODING,
	KIWI_VAR_DATESTYLE,
	KIWI_VAR_TIMEZONE,
	KIWI_VAR_STANDARD_CONFORMING_STRINGS,
	KIWI_VAR_APPLICATION_NAME,
	KIWI_VAR_COMPRESSION,
	KIWI_VAR_SEARCH_PATH,
	KIWI_VAR_STATEMENT_TIMEOUT,
	KIWI_VAR_LOCK_TIMEOUT,
	KIWI_VAR_IDLE_IN_TRANSACTION_SESSION_TIMEOUT,
	KIWI_VAR_DEFAULT_TABLE_ACCESS_METHOD,
	KIWI_VAR_DEFAULT_TOAST_COMPRESSION,
	KIWI_VAR_CHECK_FUNCTION_BODIES,
	KIWI_VAR_DEFAULT_TRANSACTION_ISOLATION,
	KIWI_VAR_DEFAULT_TRANSACTION_READ_ONLY,
	KIWI_VAR_DEFAULT_TRANSACTION_DEFERRABLE,
	KIWI_VAR_TRANSACTION_ISOLATION,
	KIWI_VAR_TRANSACTION_READ_ONLY,
	KIWI_VAR_IDLE_SESSION_TIMEOUT,
	KIWI_VAR_IS_HOT_STANDBY,
	/* greenplum */
	KIWI_VAR_GP_SESSION_ROLE,
	/* odyssey own params */
	KIWI_VAR_ODYSSEY_CATCHUP_TIMEOUT,
	KIWI_VAR_MAX,
	KIWI_VAR_UNDEF
} kiwi_var_type_t;

struct kiwi_var {
#ifdef YB_GUC_SUPPORT_VIA_SHMEM
	kiwi_var_type_t type;
#endif
	char name[KIWI_MAX_NAME_SIZE];
	int name_len;
	char value[KIWI_MAX_VAR_SIZE];
	int value_len;
};

struct kiwi_vars {
#ifndef YB_GUC_SUPPORT_VIA_SHMEM
	kiwi_var_t *vars;
	int size;
#else
	kiwi_var_t vars[KIWI_VAR_MAX];
#endif
};

static inline void kiwi_var_init(kiwi_var_t *var, char *name, int name_len)
{
#ifdef YB_GUC_SUPPORT_VIA_SHMEM
	var->type = KIWI_VAR_UNDEF;
	var->name = name;
#else
	if (name_len == 0)
		var->name[0] = '\0';
	else
		memcpy(var->name, name, name_len);
#endif
	var->name_len = name_len;
	var->value_len = 0;
}

#ifdef YB_GUC_SUPPORT_VIA_SHMEM
static inline int kiwi_var_set(kiwi_var_t *var, kiwi_var_type_t type,
			       char *value, int value_len)
{
	var->type = type;
	if (value_len > (int)sizeof(var->value))
		return -1;
	memcpy(var->value, value, value_len);
	var->value_len = value_len;
	return 0;
}

static inline void kiwi_var_unset(kiwi_var_t *var)
{
	var->type = KIWI_VAR_UNDEF;
	var->value_len = 0;
}
#endif

static inline int kiwi_var_compare(kiwi_var_t *a, kiwi_var_t *b)
{
#ifdef YB_GUC_SUPPORT_VIA_SHMEM
	if (a->type != b->type)
		return 0;
#else
	if (a == NULL || b == NULL)
		return 0;
#endif
	if (a->value_len != b->value_len)
		return 0;
	return memcmp(a->value, b->value, a->value_len) == 0;
}

#ifdef YB_GUC_SUPPORT_VIA_SHMEM
static inline kiwi_var_t *kiwi_vars_of(kiwi_vars_t *vars, kiwi_var_type_t type)
{
	return &vars->vars[type];
}

static inline kiwi_var_t *kiwi_vars_get(kiwi_vars_t *vars, kiwi_var_type_t type)
{
	if (type == KIWI_VAR_UNDEF)
		return NULL;
	if (vars->vars[type].type != KIWI_VAR_UNDEF)
		return &vars->vars[type];
	return NULL;
}
#else

/* Dynamically allocate a new GUC variable. */
static inline void yb_kiwi_var_push(kiwi_vars_t *vars, char *name, int name_len, char *value,
	int value_len)
{
	vars->size++;
	if (vars->size == 1)
		vars->vars = malloc(sizeof(kiwi_var_t));
	else
		/* TODO (rbarigidad): Double list size rather than incrementing by 1 */
		vars->vars = realloc(vars->vars, vars->size * sizeof(kiwi_var_t));

	kiwi_var_t *var = &vars->vars[vars->size - 1];
	memcpy(var->name, name, name_len);
	var->name_len = name_len;
	memcpy(var->value, value, value_len);
	var->value_len = value_len;
}

static inline int yb_kiwi_var_set(kiwi_var_t *var, char *value, int value_len)
{
	if (value_len > (int)sizeof(var->value))
		return -1;

	memcpy(var->value, value, value_len);
	var->value_len = value_len;
	return 0;
}

static inline kiwi_var_t *yb_kiwi_vars_get(kiwi_vars_t *vars, char *name)
{
	if (vars->size == 0)
		return NULL;

	for (int i = 0; i < vars->size; i++) {
		if (strcmp(vars->vars[i].name, name) == 0)
			return &vars->vars[i];
	}
	return NULL;
}
#endif

static inline void kiwi_vars_init(kiwi_vars_t *vars)
{
#ifdef YB_GUC_SUPPORT_VIA_SHMEM
	kiwi_var_init(&vars->vars[KIWI_VAR_CLIENT_ENCODING], "client_encoding",
		      16);
	kiwi_var_init(&vars->vars[KIWI_VAR_DATESTYLE], "DateStyle", 10);
	kiwi_var_init(&vars->vars[KIWI_VAR_TIMEZONE], "TimeZone", 9);
	kiwi_var_init(&vars->vars[KIWI_VAR_STANDARD_CONFORMING_STRINGS],
		      "standard_conforming_strings", 28);
	kiwi_var_init(&vars->vars[KIWI_VAR_APPLICATION_NAME],
		      "application_name", 17);
	kiwi_var_init(&vars->vars[KIWI_VAR_COMPRESSION], "compression", 12);
	kiwi_var_init(&vars->vars[KIWI_VAR_SEARCH_PATH], "search_path", 12);
	kiwi_var_init(&vars->vars[KIWI_VAR_STATEMENT_TIMEOUT],
		      "statement_timeout", sizeof("statement_timeout"));
	kiwi_var_init(&vars->vars[KIWI_VAR_LOCK_TIMEOUT], "lock_timeout",
		      sizeof("lock_timeout"));
	kiwi_var_init(&vars->vars[KIWI_VAR_IDLE_IN_TRANSACTION_SESSION_TIMEOUT],
		      "idle_in_transaction_session_timeout",
		      sizeof("idle_in_transaction_session_timeout"));
	kiwi_var_init(&vars->vars[KIWI_VAR_DEFAULT_TABLE_ACCESS_METHOD],
		      "default_table_access_method",
		      sizeof("default_table_access_method"));
	kiwi_var_init(&vars->vars[KIWI_VAR_DEFAULT_TOAST_COMPRESSION],
		      "default_toast_compression",
		      sizeof("default_toast_compression"));
	kiwi_var_init(&vars->vars[KIWI_VAR_CHECK_FUNCTION_BODIES],
		      "check_function_bodies", sizeof("check_function_bodies"));
	kiwi_var_init(&vars->vars[KIWI_VAR_DEFAULT_TRANSACTION_ISOLATION],
		      "default_transaction_isolation",
		      sizeof("default_transaction_isolation"));
	kiwi_var_init(&vars->vars[KIWI_VAR_DEFAULT_TRANSACTION_READ_ONLY],
		      "default_transaction_read_only",
		      sizeof("default_transaction_read_only"));
	kiwi_var_init(&vars->vars[KIWI_VAR_DEFAULT_TRANSACTION_DEFERRABLE],
		      "default_transaction_deferrable",
		      sizeof("default_transaction_deferrable"));
	kiwi_var_init(&vars->vars[KIWI_VAR_TRANSACTION_ISOLATION],
		      "transaction_isolation", sizeof("transaction_isolation"));
	kiwi_var_init(&vars->vars[KIWI_VAR_TRANSACTION_READ_ONLY],
		      "transaction_read_only", sizeof("transaction_read_only"));
	kiwi_var_init(&vars->vars[KIWI_VAR_IDLE_SESSION_TIMEOUT],
		      "idle_session_timeout", sizeof("idle_session_timeout"));
	kiwi_var_init(&vars->vars[KIWI_VAR_GP_SESSION_ROLE], "gp_session_role",
		      sizeof("gp_session_role"));
	kiwi_var_init(&vars->vars[KIWI_VAR_IS_HOT_STANDBY], "is_hot_standby",
		      sizeof("is_hot_standby"));
	kiwi_var_init(&vars->vars[KIWI_VAR_ODYSSEY_CATCHUP_TIMEOUT],
		      "odyssey_catchup_timeout",
		      sizeof("odyssey_catchup_timeout"));
#else
	vars->size = 0;

	/* Ensure that role is "cached" after session_authorization. */
	yb_kiwi_var_push(vars, "session_authorization", 22, "default", 8);
	yb_kiwi_var_push(vars, "role", 5, "none", 5);
#endif
}

#ifdef YB_GUC_SUPPORT_VIA_SHMEM
static inline int kiwi_vars_set(kiwi_vars_t *vars, kiwi_var_type_t type,
				char *value, int value_len)
{
	return kiwi_var_set(kiwi_vars_of(vars, type), type, value, value_len);
}

static inline void kiwi_vars_unset(kiwi_vars_t *vars, kiwi_var_type_t type)
{
	kiwi_var_unset(kiwi_vars_of(vars, type));
}

static inline kiwi_var_type_t kiwi_vars_find(kiwi_vars_t *vars, char *name,
					     int name_len)
{
	kiwi_var_type_t type;
	type = KIWI_VAR_CLIENT_ENCODING;
	for (; type < KIWI_VAR_MAX; type++) {
		kiwi_var_t *var = kiwi_vars_of(vars, type);
		if (var->name_len != name_len)
			continue;
		if (!strncasecmp(var->name, name, name_len))
			return type;
	}
	return KIWI_VAR_UNDEF;
}
#endif

static inline int kiwi_vars_update(kiwi_vars_t *vars, char *name, int name_len,
				   char *value, int value_len)
{
#ifdef YB_GUC_SUPPORT_VIA_SHMEM
	kiwi_var_type_t type;
	type = kiwi_vars_find(vars, name, name_len);
	if (type == KIWI_VAR_UNDEF)
		return -1;
	if (type == KIWI_VAR_IS_HOT_STANDBY) {
		// skip volatile params caching
		return 0;
	}
	kiwi_vars_set(vars, type, value, value_len);
#else
	/* Act as a "safe" set. (find and update, else push new value) */
	kiwi_var_t *var = yb_kiwi_vars_get(vars, name);
	if (var != NULL)
		yb_kiwi_var_set(var, value, value_len);
	else
		yb_kiwi_var_push(vars, name, name_len, value, value_len);
#endif
	return 0;
}

static inline int kiwi_vars_update_both(kiwi_vars_t *a, kiwi_vars_t *b,
					char *name, int name_len, char *value,
					int value_len)
{
#ifdef YB_GUC_SUPPORT_VIA_SHMEM
	kiwi_var_type_t type;
	type = kiwi_vars_find(a, name, name_len);
	if (type == KIWI_VAR_UNDEF)
		return -1;
	kiwi_vars_set(a, type, value, value_len);
	kiwi_vars_set(b, type, value, value_len);
#else
	kiwi_vars_update(a, name, name_len, value, value_len);
	kiwi_vars_update(b, name, name_len, value, value_len);
#endif
	return 0;
}

static inline int kiwi_vars_override(kiwi_vars_t *vars,
				     kiwi_vars_t *override_vars)
{
#ifdef YB_GUC_SUPPORT_VIA_SHMEM
	kiwi_var_type_t type = 0;
	for (; type < KIWI_VAR_MAX; type++) {
		kiwi_var_t *var = kiwi_vars_of(override_vars, type);
		if (!var->value_len)
			continue;
		kiwi_vars_set(vars, type, var->value, var->value_len);
	}
#else
	for (int i = 0; i < override_vars->size; i++) {
		if (!override_vars->vars[i].value_len)
			continue;

		kiwi_vars_update(vars, override_vars->vars[i].name,override_vars->vars[i].name_len,
			override_vars->vars[i].value, override_vars->vars[i].value_len);
	}
#endif
	return 0;
}

static inline int kiwi_enquote(char *src, char *dst, int dst_len)
{
	if (dst_len < 4)
		return -1;
	char *pos = dst;
	char *end = dst + dst_len - 4;
	*pos++ = 'E';
	*pos++ = '\'';
	while (*src && pos < end) {
		if (*src == '\'')
			*pos++ = '\'';
		else if (*src == '\\') {
			*dst++ = '\\';
		}
		*pos++ = *src++;
	}
	if (*src || pos > end)
		return -1;
	*pos++ = '\'';
	*pos = 0;
	return (int)(pos - dst);
}

__attribute__((hot)) static inline int kiwi_vars_cas(kiwi_vars_t *client,
						     kiwi_vars_t *server,
						     char *query, int query_len)
{
	int pos = 0;
#ifdef YB_GUC_SUPPORT_VIA_SHMEM
	kiwi_var_type_t type;
	type = KIWI_VAR_CLIENT_ENCODING;
	for (; type < KIWI_VAR_MAX; type++) {
		kiwi_var_t *var;
		var = kiwi_vars_of(client, type);
		/* we do not support odyssey-to-backend compression yet */
		if (var->type == KIWI_VAR_UNDEF ||
		    var->type == KIWI_VAR_COMPRESSION)
			continue;
		kiwi_var_t *server_var;
		server_var = kiwi_vars_of(server, type);
#else
	kiwi_var_t *var;
	for (int i = 0; i < client->size; i++) {
		var = &client->vars[i];
		/* we do not support odyssey-to-backend compression yet */

		if (strcmp(var->name, "compression") == 0)
			continue;
		kiwi_var_t *server_var;
		server_var = yb_kiwi_vars_get(server, var->name);
#endif
		if (kiwi_var_compare(var, server_var))
			continue;

		/* SET key=quoted_value; */
		int size = 4 + (var->name_len - 1) + 1 + 1;
		if (query_len < size)
			return -1;
		memcpy(query + pos, "SET ", 4);
		pos += 4;
		memcpy(query + pos, var->name, var->name_len - 1);
		pos += var->name_len - 1;
		memcpy(query + pos, "=", 1);
		pos += 1;

		/* Do not enquote the default values for auth related params. */
		if (!(strcmp(var->name, "role") == 0 && strcmp(var->value, "none") == 0) &&
			!(strcmp(var->name, "session_authorization") == 0 && strcmp(var->value, "default") == 0)) {
			int quote_len;
			quote_len =
				kiwi_enquote(var->value, query + pos, query_len - pos);
			if (quote_len == -1)
				return -1;
			pos += quote_len;
		} else {
			memcpy(query + pos, var->value, var->value_len - 1);
			pos += var->value_len - 1;
		}
		memcpy(query + pos, ";", 1);
		pos += 1;
	}

	return pos;
}

#endif /* KIWI_VAR_H */
