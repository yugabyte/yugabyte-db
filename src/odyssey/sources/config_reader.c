

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <kiwi.h>
#include <machinarium.h>
#include <odyssey.h>

#include "yb/yql/ysql_conn_mgr_wrapper/ysql_conn_mgr_stats.h"

typedef enum {
	OD_LYES,
	OD_LNO,
	OD_LINCLUDE,
	OD_LDAEMONIZE,
	OD_LPRIORITY,
	OD_LLOG_TO_STDOUT,
	OD_LLOG_DEBUG,
	OD_LLOG_CONFIG,
	OD_LLOG_SESSION,
	OD_LLOG_QUERY,
	OD_LLOG_FILE,
	OD_LLOG_FORMAT,
	OD_LLOG_STATS,

	/* Prometheus */
	OD_LLOG_GENERAL_STATS_PROM,
	OD_LLOG_ROUTE_STATS_PROM,
	OD_LPROMHTTP_PORT,

	OD_LPID_FILE,
	OD_LUNIX_SOCKET_DIR,
	OD_LUNIX_SOCKET_MODE,
	OD_LLOCKS_DIR,
	OD_LENABLE_ONLINE_RESTART,
	OD_LGRACEFUL_DIE_ON_ERRORS,
	OD_LBINDWITH_REUSEPORT,
	OD_LLOG_SYSLOG,
	OD_LLOG_SYSLOG_IDENT,
	OD_LLOG_SYSLOG_FACILITY,
	OD_LSTATS_INTERVAL,
	OD_LLISTEN,
	OD_LHOST,
	OD_LPORT,
	OD_LTARGET_SESSION_ATTRS,
	OD_LBACKLOG,
	OD_LNODELAY,
	OD_LKEEPALIVE,
	OD_LKEEPALIVE_INTERVAL,
	OD_LKEEPALIVE_PROBES,
	OD_LKEEPALIVE_USR_TIMEOUT,
	OD_LREADAHEAD,
	OD_LWORKERS,
	OD_LRESOLVERS,
	OD_LPIPELINE,
	OD_LPACKET_READ_SIZE,
	OD_LPACKET_WRITE_QUEUE,
	OD_LCACHE,
	OD_LCACHE_CHUNK,
	OD_LCACHE_MSG_GC_SIZE,
	OD_LCACHE_COROUTINE,
	OD_LCOROUTINE_STACK_SIZE,
	OD_LCLIENT_MAX,
	OD_LCLIENT_MAX_ROUTING,
	OD_LSERVER_LOGIN_RETRY,
	OD_LCLIENT_LOGIN_TIMEOUT,
	OD_LCLIENT_FWD_ERROR,
	OD_LPRESERVE_SESSION_SERVER_CONN,
	OD_LAPPLICATION_NAME_ADD_HOST,
	OD_LSERVER_LIFETIME,
	OD_LMIN_POOL_SIZE,
	OD_LTLS,
	OD_LTLS_CA_FILE,
	OD_LTLS_KEY_FILE,
	OD_LTLS_CERT_FILE,
	OD_LTLS_PROTOCOLS,
	OD_LCOMPRESSION,
	OD_LSTORAGE,
	OD_LTYPE,
	OD_LSERVERS_MAX_ROUTING,
	OD_LDEFAULT,
	OD_LDATABASE,
	OD_LUSER,
	OD_LPASSWORD,
	OD_LROLE,
	OD_LPOOL,
	OD_LPOOL_ROUTING,
	OD_LPOOL_PRESERVE_PREP_STMT,
#ifdef LDAP_FOUND
	OD_LLDAPPOOL_SIZE,
	OD_LLDAPPOOL_TIMEOUT,
	OD_LLDAPPOOL_TTL,
#endif
	OD_LPOOL_SIZE,
	OD_LPOOL_TIMEOUT,
	OD_LPOOL_TTL,
	OD_LPOOL_DISCARD,
	OD_LPOOL_SMART_DISCARD,
	OD_LPOOL_CANCEL,
	OD_LPOOL_ROLLBACK,
	OD_LPOOL_RESERVE_PREPARED_STATEMENT,
	OD_LPOOL_CLIENT_IDLE_TIMEOUT,
	OD_LPOOL_IDLE_IN_TRANSACTION_TIMEOUT,
	OD_LSTORAGE_DB,
	OD_LSTORAGE_USER,
	OD_LSTORAGE_PASSWORD,
	OD_LAUTHENTICATION,
	OD_LAUTH_COMMON_NAME,
	OD_LAUTH_PAM_SERVICE,
	OD_LAUTH_MODULE,
	OD_LAUTH_QUERY,
	OD_LAUTH_QUERY_DB,
	OD_LAUTH_QUERY_USER,
	OD_LAUTH_LDAP_SERVICE,
	OD_LAUTH_PASSWORD_PASSTHROUGH,
	OD_LQUANTILES,
	OD_LMODULE,
	OD_LLDAP_ENDPOINT,
	OD_LLDAP_SERVER,
	OD_LLDAP_PORT,
	OD_LLDAP_PREFIX,
	OD_LLDAP_SUFFIX,
	OD_LLDAP_BASEDN,
	OD_LLDAP_BINDDN,
	OD_LLDAP_URL,
	OD_LLDAP_SEARCH_ATTRIBUTE,
	OD_LLDAP_BIND_PASSWD,
	OD_LLDAP_SCHEME,
	OD_LLDAP_SCOPE,
	OD_LLDAP_SEARCH_FILTER,
	OD_LLDAP_ENDPOINT_NAME,
	OD_LLDAP_STORAGE_CREDENTIALS_ATTR,
	OD_LLDAP_STORAGE_CREDENTIALS,
	OD_LLDAP_STORAGE_USERNAME,
	OD_LLDAP_STORAGE_PASSWORD,
	OD_LWATCHDOG,
	OD_LWATCHDOG_LAG_QUERY,
	OD_LWATCHDOG_LAG_INTERVAL,
	OD_LCATCHUP_TIMEOUT,
	OD_LCATCHUP_CHECKS,
	OD_LOPTIONS,
	OD_LHBA_FILE,
} od_lexeme_t;

static od_keyword_t od_config_keywords[] = {
	/* main */
	od_keyword("yes", OD_LYES),
	od_keyword("no", OD_LNO),
	od_keyword("include", OD_LINCLUDE),
	od_keyword("daemonize", OD_LDAEMONIZE),
	od_keyword("priority", OD_LPRIORITY),
	od_keyword("pid_file", OD_LPID_FILE),
	od_keyword("unix_socket_dir", OD_LUNIX_SOCKET_DIR),
	od_keyword("unix_socket_mode", OD_LUNIX_SOCKET_MODE),
	od_keyword("locks_dir", OD_LLOCKS_DIR),

	od_keyword("enable_online_restart", OD_LENABLE_ONLINE_RESTART),
	od_keyword("graceful_die_on_errors", OD_LGRACEFUL_DIE_ON_ERRORS),
	od_keyword("bindwith_reuseport", OD_LBINDWITH_REUSEPORT),

	/* logging */
	od_keyword("log_debug", OD_LLOG_DEBUG),
	od_keyword("log_to_stdout", OD_LLOG_TO_STDOUT),
	od_keyword("log_config", OD_LLOG_CONFIG),
	od_keyword("log_session", OD_LLOG_SESSION),
	od_keyword("log_query", OD_LLOG_QUERY),
	od_keyword("log_file", OD_LLOG_FILE),
	od_keyword("log_format", OD_LLOG_FORMAT),
	od_keyword("log_stats", OD_LLOG_STATS),
	od_keyword("log_syslog", OD_LLOG_SYSLOG),
	od_keyword("log_syslog_ident", OD_LLOG_SYSLOG_IDENT),
	od_keyword("log_syslog_facility", OD_LLOG_SYSLOG_FACILITY),
	od_keyword("stats_interval", OD_LSTATS_INTERVAL),

	/* Prometheus */
	od_keyword("log_general_stats_prom", OD_LLOG_GENERAL_STATS_PROM),
	od_keyword("log_route_stats_prom", OD_LLOG_ROUTE_STATS_PROM),
	od_keyword("promhttp_server_port", OD_LPROMHTTP_PORT),

	/* listen */
	od_keyword("listen", OD_LLISTEN),
	od_keyword("host", OD_LHOST),
	od_keyword("port", OD_LPORT),
	/* target_session_attrs */
	od_keyword("target_session_attrs", OD_LTARGET_SESSION_ATTRS),
	od_keyword("backlog", OD_LBACKLOG),
	od_keyword("nodelay", OD_LNODELAY),

	/* TCP keepalive */
	od_keyword("keepalive", OD_LKEEPALIVE),
	od_keyword("keepalive_keep_interval", OD_LKEEPALIVE_INTERVAL),
	od_keyword("keepalive_probes", OD_LKEEPALIVE_PROBES),
	od_keyword("keepalive_usr_timeout", OD_LKEEPALIVE_USR_TIMEOUT),
	/*              */

	od_keyword("readahead", OD_LREADAHEAD),
	od_keyword("workers", OD_LWORKERS),
	od_keyword("resolvers", OD_LRESOLVERS),
	od_keyword("pipeline", OD_LPIPELINE),
	od_keyword("packet_read_size", OD_LPACKET_READ_SIZE),
	od_keyword("packet_write_queue", OD_LPACKET_WRITE_QUEUE),
	od_keyword("cache", OD_LCACHE),
	od_keyword("cache_chunk", OD_LCACHE_CHUNK),
	od_keyword("cache_msg_gc_size", OD_LCACHE_MSG_GC_SIZE),
	od_keyword("cache_coroutine", OD_LCACHE_COROUTINE),
	od_keyword("coroutine_stack_size", OD_LCOROUTINE_STACK_SIZE),
	/* client */
	od_keyword("client_max", OD_LCLIENT_MAX),
	od_keyword("client_max_routing", OD_LCLIENT_MAX_ROUTING),
	od_keyword("server_login_retry", OD_LSERVER_LOGIN_RETRY),
	od_keyword("client_login_timeout", OD_LCLIENT_LOGIN_TIMEOUT),
	od_keyword("client_fwd_error", OD_LCLIENT_FWD_ERROR),
	od_keyword("reserve_session_server_connection",
		   OD_LPRESERVE_SESSION_SERVER_CONN),
	od_keyword("application_name_add_host", OD_LAPPLICATION_NAME_ADD_HOST),
	od_keyword("server_lifetime", OD_LSERVER_LIFETIME),
	od_keyword("min_pool_size", OD_LMIN_POOL_SIZE),

	/*   tls */
	od_keyword("tls", OD_LTLS),
	od_keyword("tls_ca_file", OD_LTLS_CA_FILE),
	od_keyword("tls_key_file", OD_LTLS_KEY_FILE),
	od_keyword("tls_cert_file", OD_LTLS_CERT_FILE),
	od_keyword("tls_protocols", OD_LTLS_PROTOCOLS),
	od_keyword("compression", OD_LCOMPRESSION),

	/* storage */
	od_keyword("storage", OD_LSTORAGE),
	od_keyword("type", OD_LTYPE),
	od_keyword("server_max_routing", OD_LSERVERS_MAX_ROUTING),
	od_keyword("default", OD_LDEFAULT),

	/* database */
	od_keyword("database", OD_LDATABASE),
	od_keyword("user", OD_LUSER),
	od_keyword("password", OD_LPASSWORD),
	od_keyword("role", OD_LROLE),
	od_keyword("pool", OD_LPOOL),

	od_keyword("pool_preserve_prep_stmt", OD_LPOOL_PRESERVE_PREP_STMT),

	od_keyword("pool_routing", OD_LPOOL_ROUTING),
#ifdef LDAP_FOUND
	od_keyword("ldap_pool_size", OD_LLDAPPOOL_SIZE),
	od_keyword("ldap_pool_timeout", OD_LLDAPPOOL_TIMEOUT),
	od_keyword("ldap_pool_ttl", OD_LLDAPPOOL_TTL),
#endif
	od_keyword("pool_size", OD_LPOOL_SIZE),
	od_keyword("pool_timeout", OD_LPOOL_TIMEOUT),
	od_keyword("pool_ttl", OD_LPOOL_TTL),
	od_keyword("pool_discard", OD_LPOOL_DISCARD),
	od_keyword("pool_smart_discard", OD_LPOOL_SMART_DISCARD),
	od_keyword("pool_cancel", OD_LPOOL_CANCEL),
	od_keyword("pool_rollback", OD_LPOOL_ROLLBACK),
	od_keyword("pool_reserve_prepared_statement",
		   OD_LPOOL_RESERVE_PREPARED_STATEMENT),
	od_keyword("pool_client_idle_timeout", OD_LPOOL_CLIENT_IDLE_TIMEOUT),
	od_keyword("pool_idle_in_transaction_timeout",
		   OD_LPOOL_IDLE_IN_TRANSACTION_TIMEOUT),
	od_keyword("storage_db", OD_LSTORAGE_DB),
	od_keyword("storage_user", OD_LSTORAGE_USER),
	od_keyword("storage_password", OD_LSTORAGE_PASSWORD),

	/* auth */
	od_keyword("authentication", OD_LAUTHENTICATION),
	od_keyword("auth_common_name", OD_LAUTH_COMMON_NAME),
	od_keyword("auth_query", OD_LAUTH_QUERY),
	od_keyword("auth_query_db", OD_LAUTH_QUERY_DB),
	od_keyword("auth_query_user", OD_LAUTH_QUERY_USER),
	od_keyword("auth_pam_service", OD_LAUTH_PAM_SERVICE),
	od_keyword("auth_module", OD_LAUTH_MODULE),
	od_keyword("password_passthrough", OD_LAUTH_PASSWORD_PASSTHROUGH),
	od_keyword("load_module", OD_LMODULE),
	od_keyword("hba_file", OD_LHBA_FILE),

	/* ldap */
	od_keyword("ldap_endpoint", OD_LLDAP_ENDPOINT),
	od_keyword("ldapserver", OD_LLDAP_SERVER),
	od_keyword("ldapport", OD_LLDAP_PORT),
	od_keyword("ldapprefix", OD_LLDAP_PREFIX),
	od_keyword("ldapsuffix", OD_LLDAP_SUFFIX),
	od_keyword("ldapbasedn", OD_LLDAP_BASEDN),
	od_keyword("ldapbinddn", OD_LLDAP_BINDDN),
	od_keyword("ldapbindpasswd", OD_LLDAP_BIND_PASSWD),
	od_keyword("ldapurl", OD_LLDAP_URL),
	od_keyword("ldapsearchattribute", OD_LLDAP_SEARCH_ATTRIBUTE),
	od_keyword("ldapscheme", OD_LLDAP_SCHEME),
	od_keyword("ldapsearchfilter", OD_LLDAP_SEARCH_FILTER),
	od_keyword("ldapscope", OD_LLDAP_SCOPE),
	od_keyword("ldap_endpoint_name", OD_LLDAP_ENDPOINT_NAME),
	od_keyword("ldap_storage_credentials_attr",
		   OD_LLDAP_STORAGE_CREDENTIALS_ATTR),
	od_keyword("ldap_storage_credentials", OD_LLDAP_STORAGE_CREDENTIALS),
	od_keyword("ldap_storage_username", OD_LLDAP_STORAGE_USERNAME),
	od_keyword("ldap_storage_password", OD_LLDAP_STORAGE_PASSWORD),

	/* watchdog */

	od_keyword("watchdog", OD_LWATCHDOG),
	od_keyword("watchdog_lag_query", OD_LWATCHDOG_LAG_QUERY),
	od_keyword("watchdog_lag_interval", OD_LWATCHDOG_LAG_INTERVAL),
	od_keyword("catchup_timeout", OD_LCATCHUP_TIMEOUT),
	od_keyword("catchup_checks", OD_LCATCHUP_CHECKS),

	/* options */

	od_keyword("options", OD_LOPTIONS),

	/* stats */
	od_keyword("quantiles", OD_LQUANTILES),
	{ 0, 0, 0 },
};

static od_keyword_t od_role_keywords[] = {
	od_keyword("admin", OD_RULE_ROLE_ADMIN),
	od_keyword("stat", OD_RULE_ROLE_STAT),
	od_keyword("notallow", OD_RULE_ROLE_NOTALLOW),
	{ 0, 0, 0 },
};

static inline int od_config_reader_watchdog(od_config_reader_t *reader,
					    od_storage_watchdog_t *watchdog,
					    od_extention_t *extentions);

static int od_config_reader_open(od_config_reader_t *reader, char *config_file)
{
	reader->config_file = config_file;
	/* read file */
	char *config_buf = NULL;
	FILE *file = fopen(config_file, "r");
	if (file == NULL)
		goto error;
	fseek(file, 0, SEEK_END);
	int size = (int)ftell(file);
	if (size == -1)
		goto error;
	fseek(file, 0, SEEK_SET);
	config_buf = malloc(size);
	if (config_buf == NULL)
		goto error;
	int rc = fread(config_buf, size, 1, file);
	if (rc != 1) {
		free(config_buf);
		goto error;
	}
	switch (fclose(file)) {
	case 0: {
		reader->data = config_buf;
		reader->data_size = size;

		od_parser_init(&reader->parser, reader->data,
			       reader->data_size);
		return 0;
	}
	case EOF: {
		od_errorf(reader->error, "failed to close config file '%s': %d",
			  config_file, errno);
		free(config_buf);
		return NOT_OK_RESPONSE;
	}
	default:
		assert(0);
	}

error:
	od_errorf(reader->error, "failed to open config file '%s'",
		  config_file);
	if (file) {
		fclose(file);
	}
	return NOT_OK_RESPONSE;
}

static void od_config_reader_close(od_config_reader_t *reader)
{
	if (reader->data_size > 0)
		free(reader->data);
}

static bool od_config_reader_is(od_config_reader_t *reader, int id)
{
	od_token_t token;
	int rc;
	token.line = 0;
	rc = od_parser_next(&reader->parser, &token);
	od_parser_push(&reader->parser, &token);
	if (rc != id)
		return false;
	return true;
}

bool od_config_reader_keyword(od_config_reader_t *reader, od_keyword_t *keyword)
{
	od_token_t token;
	int rc;
	rc = od_parser_next(&reader->parser, &token);
	if (rc != OD_PARSER_KEYWORD)
		goto error;
	od_keyword_t *match;
	match = od_keyword_match(od_config_keywords, &token);
	if (keyword == NULL)
		goto error;
	if (keyword != match)
		goto error;
	return true;
error:
	od_parser_push(&reader->parser, &token);
	char *kwname = "unknown";
	if (keyword)
		kwname = keyword->name;
	od_config_reader_error(reader, &token, "expected '%s'", kwname);
	return false;
}

static bool od_config_reader_quantiles(od_config_reader_t *reader, char *value,
				       double **quantiles, int *count)
{
	int comma_cnt = 1;
	char *c = value;
	while (*c) {
		if (*c == ',')
			comma_cnt++;
		c++;
	}
	*quantiles = malloc(sizeof(double) * comma_cnt);
	double *array = *quantiles;
	*count = 0;
	c = value;
	while (*c) {
		int length = sscanf(c, "%lf", array + *count);
		if (length != 1 || array[*count] > 1 || array[*count] < 0) {
			od_config_reader_error(reader, NULL,
					       "incorrect quantile value");
			free(*quantiles);
			return false;
		}
		*count += 1;
		while (*c != '\0' && *c != ',') {
			c++;
		}
		if (*c == '\0')
			break;
		c++;
	}
	return true;
}

static bool od_config_reader_string(od_config_reader_t *reader, char **value)
{
	od_token_t token;
	int rc;
	rc = od_parser_next(&reader->parser, &token);
	if (rc != OD_PARSER_STRING) {
		od_parser_push(&reader->parser, &token);
		od_config_reader_error(reader, &token, "expected 'string'");
		return false;
	}
	char *copy = malloc(token.value.string.size + 1);
	if (copy == NULL) {
		od_parser_push(&reader->parser, &token);
		od_config_reader_error(reader, &token,
				       "memory allocation error");
		return false;
	}
	memcpy(copy, token.value.string.pointer, token.value.string.size);
	copy[token.value.string.size] = 0;
	if (*value)
		free(*value);
	*value = copy;
	return true;
}

static bool od_config_reader_number(od_config_reader_t *reader, int *number)
{
	od_token_t token;
	int rc;
	rc = od_parser_next(&reader->parser, &token);
	if (rc != OD_PARSER_NUM) {
		od_parser_push(&reader->parser, &token);
		od_config_reader_error(reader, &token, "expected 'number'");
		return false;
	}
	/* uint64 to int conversion */
	*number = token.value.num;
	return true;
}

static bool od_config_reader_number64(od_config_reader_t *reader,
				      uint64_t *number)
{
	od_token_t token;
	int rc;
	rc = od_parser_next(&reader->parser, &token);
	if (rc != OD_PARSER_NUM) {
		od_parser_push(&reader->parser, &token);
		od_config_reader_error(reader, &token, "expected 'number'");
		return false;
	}
	*number = token.value.num;
	return true;
}

static bool od_config_reader_yes_no(od_config_reader_t *reader, int *value)
{
	od_token_t token;
	int rc;
	rc = od_parser_next(&reader->parser, &token);
	if (rc != OD_PARSER_KEYWORD)
		goto error;
	od_keyword_t *keyword;
	keyword = od_keyword_match(od_config_keywords, &token);
	if (keyword == NULL)
		goto error;
	switch (keyword->id) {
	case OD_LYES:
		*value = 1;
		break;
	case OD_LNO:
		*value = 0;
		break;
	default:
		goto error;
	}
	return true;
error:
	od_parser_push(&reader->parser, &token);
	od_config_reader_error(reader, &token, "expected 'yes/no'");
	return false;
}

static int od_config_reader_storage_host(od_config_reader_t *reader,
					 od_rule_storage_t *storage)
{
	size_t i;
	size_t j;
	size_t tmp;
	size_t len;
	size_t endpoint_cnt;
	size_t closing_bracked_indx;
	size_t host_len;
	size_t host_off;

	if (!od_config_reader_string(reader, &storage->host)) {
		return NOT_OK_RESPONSE;
	}

	endpoint_cnt = 0;
	len = strlen(storage->host);

	/* string in format host[,host...] */
	for (i = 0; i < len; i++) {
		if (storage->host[i] == ',') {
			++endpoint_cnt;
		}
	}
	++endpoint_cnt;

	storage->endpoints_count = 0;
	storage->endpoints =
		malloc(sizeof(od_storage_endpoint_t) * endpoint_cnt);

	for (i = 0; i < len;) {
		closing_bracked_indx = len + 1;

		for (j = i; j + 1 < len && storage->host[j + 1] != ','; ++j) {
			switch (storage->host[j]) {
			case '[':
				if (j > i) {
					return NOT_OK_RESPONSE; /* wrong entry format */
				}
				break;
			case ']':
				if (closing_bracked_indx < j) {
					return NOT_OK_RESPONSE; /* wrong entry format */
				}
				closing_bracked_indx = j;
				break;
			}
		}

		if (storage->host[i] != '[') {
			/* block format is  host[,host] */
			host_len = j - i + 1;
			host_off = i;

			storage->endpoints[storage->endpoints_count].port = 0;
		} else {
			if (closing_bracked_indx == len + 1) {
				/* matching bracked was not met */
				return NOT_OK_RESPONSE;
			}
			/* [host]:port */

			host_len = closing_bracked_indx - i - 1;
			host_off = i + 1;

			storage->endpoints[storage->endpoints_count].port = 0;
			/*    ]:1234 */
			/*      ^  ^ */
			/*      iter betwwen this two localtions */

			for (tmp = closing_bracked_indx + 2; tmp <= j; ++tmp) {
				if (!isdigit(storage->host[tmp])) {
					return NOT_OK_RESPONSE;
				}
				storage->endpoints[storage->endpoints_count]
					.port *= 10;
				storage->endpoints[storage->endpoints_count]
					.port += storage->host[tmp] - '0';
			}
		}

		/* copy the host name */
		storage->endpoints[storage->endpoints_count].host =
			malloc(sizeof(char) * (host_len + 1));
		memcpy(storage->endpoints[storage->endpoints_count].host,
		       storage->host + host_off, host_len);
		storage->endpoints[storage->endpoints_count].host[host_len] =
			'\0';

		storage->endpoints_count++;

		/* storage->host[j] == ',' or j == len - 1 */
		i = j + 2;
	}

	return OK_RESPONSE;
}

static int od_config_reader_listen(od_config_reader_t *reader)
{
	od_config_t *config = reader->config;

	od_config_listen_t *listen;
	listen = od_config_listen_add(config);
	if (listen == NULL) {
		return NOT_OK_RESPONSE;
	}

	/* { */
	if (!od_config_reader_symbol(reader, '{'))
		return NOT_OK_RESPONSE;

	for (;;) {
		od_token_t token;
		int rc;
		rc = od_parser_next(&reader->parser, &token);
		switch (rc) {
		case OD_PARSER_KEYWORD:
			break;
		case OD_PARSER_EOF:
			od_config_reader_error(reader, &token,
					       "unexpected end of config file");
			return NOT_OK_RESPONSE;
		case OD_PARSER_SYMBOL:
			/* } */
			if (token.value.num == '}') {
				return OK_RESPONSE;
			}
			/* fall through */
		default:
			od_config_reader_error(
				reader, &token,
				"incorrect or unexpected parameter");
			return NOT_OK_RESPONSE;
		}
		od_keyword_t *keyword;
		keyword = od_keyword_match(od_config_keywords, &token);
		if (keyword == NULL) {
			od_config_reader_error(reader, &token,
					       "unknown parameter");
			return NOT_OK_RESPONSE;
		}
		switch (keyword->id) {
		/* host */
		case OD_LHOST:
			if (!od_config_reader_string(reader, &listen->host))
				return NOT_OK_RESPONSE;
			continue;
		/* port */
		case OD_LPORT:
			if (!od_config_reader_number(reader, &listen->port))
				return NOT_OK_RESPONSE;
			continue;
		/* client_login_timeout */
		case OD_LCLIENT_LOGIN_TIMEOUT:
			if (!od_config_reader_number(
				    reader, &listen->client_login_timeout))
				return NOT_OK_RESPONSE;
			continue;
		/* backlog */
		case OD_LBACKLOG:
			if (!od_config_reader_number(reader, &listen->backlog))
				return NOT_OK_RESPONSE;
			continue;
		/* tls */
		case OD_LTLS:
			if (!od_config_reader_string(reader,
						     &listen->tls_opts->tls))
				return NOT_OK_RESPONSE;
			continue;
		/* tls_ca_file */
		case OD_LTLS_CA_FILE:
			if (!od_config_reader_string(
				    reader, &listen->tls_opts->tls_ca_file))
				return NOT_OK_RESPONSE;
			continue;
		/* tls_key_file */
		case OD_LTLS_KEY_FILE:
			if (!od_config_reader_string(
				    reader, &listen->tls_opts->tls_key_file))
				return NOT_OK_RESPONSE;
			continue;
		/* tls_cert_file */
		case OD_LTLS_CERT_FILE:
			if (!od_config_reader_string(
				    reader, &listen->tls_opts->tls_cert_file))
				return NOT_OK_RESPONSE;
			continue;
		/* tls_protocols */
		case OD_LTLS_PROTOCOLS:
			if (!od_config_reader_string(
				    reader, &listen->tls_opts->tls_protocols))
				return NOT_OK_RESPONSE;
			continue;
		/* compression */
		case OD_LCOMPRESSION:
			if (!od_config_reader_yes_no(reader,
						     &listen->compression))
				return NOT_OK_RESPONSE;
			continue;
		default:
			od_config_reader_error(reader, &token,
					       "unexpected parameter");
			return NOT_OK_RESPONSE;
		}
	}
	/* unreach */
	return NOT_OK_RESPONSE;
}

static int od_config_reader_storage(od_config_reader_t *reader,
				    od_extention_t *extentions)
{
	char *tmp = NULL;
	od_rule_storage_t *storage;
	storage = od_rules_storage_allocate();
	if (storage == NULL)
		return NOT_OK_RESPONSE;

	/* name */
	if (!od_config_reader_string(reader, &storage->name))
		return NOT_OK_RESPONSE;

	if (od_rules_storage_match(reader->rules, storage->name) != NULL) {
		od_config_reader_error(reader, NULL,
				       "duplicate storage definition: %s",
				       storage->name);
		return NOT_OK_RESPONSE;
	}
	od_rules_storage_add(reader->rules, storage);
	/* { */
	if (!od_config_reader_symbol(reader, '{'))
		return NOT_OK_RESPONSE;

	for (;;) {
		od_token_t token;
		int rc;
		rc = od_parser_next(&reader->parser, &token);
		switch (rc) {
		case OD_PARSER_KEYWORD:
			break;
		case OD_PARSER_EOF:
			od_config_reader_error(reader, &token,
					       "unexpected end of config file");
			return NOT_OK_RESPONSE;
		case OD_PARSER_SYMBOL:
			/* } */
			if (token.value.num == '}') {
				return OK_RESPONSE;
			}
			/* fall through */
		default:
			od_config_reader_error(
				reader, &token,
				"incorrect or unexpected parameter");
			return NOT_OK_RESPONSE;
		}
		od_keyword_t *keyword;
		keyword = od_keyword_match(od_config_keywords, &token);
		if (keyword == NULL) {
			od_config_reader_error(reader, &token,
					       "unknown parameter");
			return NOT_OK_RESPONSE;
		}

		switch (keyword->id) {
		/* type */
		case OD_LTYPE:
			if (!od_config_reader_string(reader, &storage->type))
				return NOT_OK_RESPONSE;
			continue;
		/* host */
		case OD_LHOST:
			if (od_config_reader_storage_host(reader, storage) !=
			    OK_RESPONSE)
				return NOT_OK_RESPONSE;
			continue;
		/* port */
		case OD_LPORT:
			if (!od_config_reader_number(reader, &storage->port))
				return NOT_OK_RESPONSE;
			continue;
		/* target_session_attrs */
		case OD_LTARGET_SESSION_ATTRS:
			if (!od_config_reader_string(reader, &tmp)) {
				return NOT_OK_RESPONSE;
			}

			if (strcmp(tmp, "read-write") == 0) {
				storage->target_session_attrs =
					OD_TARGET_SESSION_ATTRS_RW;
			} else if (strcmp(tmp, "any") == 0) {
				storage->target_session_attrs =
					OD_TARGET_SESSION_ATTRS_ANY;
			} else if (strcmp(tmp, "read-only") == 0) {
				storage->target_session_attrs =
					OD_TARGET_SESSION_ATTRS_RO;
			} else {
				return NOT_OK_RESPONSE;
			}

			free(tmp);
			tmp = NULL;

			continue;
		/* tls */
		case OD_LTLS:
			if (!od_config_reader_string(reader,
						     &storage->tls_opts->tls))
				return NOT_OK_RESPONSE;
			continue;
		/* tls_ca_file */
		case OD_LTLS_CA_FILE:
			if (!od_config_reader_string(
				    reader, &storage->tls_opts->tls_ca_file))
				return NOT_OK_RESPONSE;
			continue;
		/* tls_key_file */
		case OD_LTLS_KEY_FILE:
			if (!od_config_reader_string(
				    reader, &storage->tls_opts->tls_key_file))
				return NOT_OK_RESPONSE;
			continue;
		/* tls_cert_file */
		case OD_LTLS_CERT_FILE:
			if (!od_config_reader_string(
				    reader, &storage->tls_opts->tls_cert_file))
				return NOT_OK_RESPONSE;
			continue;
		/* tls_protocols */
		case OD_LTLS_PROTOCOLS:
			if (!od_config_reader_string(
				    reader, &storage->tls_opts->tls_protocols))
				return NOT_OK_RESPONSE;
			continue;
		/* server_max_routing */
		case OD_LSERVERS_MAX_ROUTING:
			if (!od_config_reader_number(
				    reader, &storage->server_max_routing))
				return NOT_OK_RESPONSE;
			continue;
		/* watchdog */
		case OD_LWATCHDOG:
			storage->watchdog =
				od_storage_watchdog_allocate(reader->global);
			if (storage->watchdog == NULL) {
				return NOT_OK_RESPONSE;
			}
			if (od_config_reader_watchdog(reader, storage->watchdog,
						      extentions) ==
			    NOT_OK_RESPONSE)
				return NOT_OK_RESPONSE;
			continue;
		default:
			od_config_reader_error(reader, &token,
					       "unexpected parameter");
			return NOT_OK_RESPONSE;
		}
	}
	/* unreach */
	return NOT_OK_RESPONSE;
}

static inline int od_config_reader_pgoptions_kv_pair(
	od_config_reader_t *reader, od_token_t *token, char **optarg,
	size_t *optarg_len, char **optval, size_t *optval_len)
{
	*optarg_len = token->value.string.size;
	*optarg = malloc(*optarg_len + 1);
	if (*optarg == NULL) {
		return NOT_OK_RESPONSE;
	}
	memcpy(*optarg, token->value.string.pointer, token->value.string.size);
	(*optarg)[*optarg_len] = 0;

	int rc;
	rc = od_parser_next(&reader->parser, token);
	if (rc != OD_PARSER_STRING) {
		free(*optarg);
		return NOT_OK_RESPONSE;
	}
	*optval_len = token->value.string.size;
	*optval = malloc(*optval_len + 1);
	if (*optval == NULL) {
		free(*optarg);
		return NOT_OK_RESPONSE;
	}
	memcpy(*optval, token->value.string.pointer, token->value.string.size);
	(*optval)[*optval_len] = 0;

	return OK_RESPONSE;
}

static inline int od_config_reader_pgoptions(od_config_reader_t *reader,
					     kiwi_vars_t *dest)
{
	od_token_t token;
	int rc;
	rc = od_parser_next(&reader->parser, &token);
	switch (rc) {
	case OD_PARSER_KEYWORD:
		break;
	case OD_PARSER_EOF:
		od_config_reader_error(reader, &token,
				       "unexpected end of config file");
		return NOT_OK_RESPONSE;
	case OD_PARSER_SYMBOL:
		/* { */
		if (token.value.num == '{')
			break;
		/* fall through */
	default:
		od_config_reader_error(reader, &token,
				       "incorrect or unexpected parameter");
		return NOT_OK_RESPONSE;
	}

	char *optarg = NULL, *optval = NULL;
	size_t optarg_len, optval_len;

	for (;;) {
		rc = od_parser_next(&reader->parser, &token);
		switch (rc) {
		case OD_PARSER_STRING:
			if (od_config_reader_pgoptions_kv_pair(
				    reader, &token, &optarg, &optarg_len,
				    &optval, &optval_len) == NOT_OK_RESPONSE) {
				return NOT_OK_RESPONSE;
			}
			kiwi_vars_update(dest, optarg, optarg_len + 1, optval,
					 optval_len + 1);
			free(optarg);
			free(optval);
			break;
		case OD_PARSER_EOF:
			od_config_reader_error(reader, &token,
					       "unexpected end of config file");
			return NOT_OK_RESPONSE;
		case OD_PARSER_SYMBOL:
			/* } */
			if (token.value.num == '}')
				return 0;
			/* fall through */
		case OD_PARSER_KEYWORD:
		default:
			od_config_reader_error(
				reader, &token,
				"incorrect or unexpected parameter");
			return NOT_OK_RESPONSE;
		}
	}
}

#ifdef LDAP_FOUND

static inline od_retcode_t
od_config_reader_ldap_storage_credentials(od_config_reader_t *reader,
					  od_rule_t *rule)
{
	od_ldap_storage_credentials_t *lsc_current;
	lsc_current = od_ldap_storage_credentials_alloc();
	if (!lsc_current) {
		goto error;
	}

	/* name */
	if (!od_config_reader_string(reader, &lsc_current->name)) {
		goto error;
	}

	if (strlen(lsc_current->name) == 0) {
		od_config_reader_error(
			reader, NULL,
			"empty ldap storage credentials definition");
		goto error;
	}

	if (od_ldap_storage_credentials_find(&rule->ldap_storage_creds_list,
					     lsc_current->name) != NULL) {
		od_config_reader_error(
			reader, NULL,
			"duplicate ldap storage credentials definition: %s",
			lsc_current->name);
		goto error;
	}

	od_rule_ldap_storage_credentials_add(rule, lsc_current);

	/* { */
	if (!od_config_reader_symbol(reader, '{')) {
		goto error;
	}

	for (;;) {
		od_token_t token;
		int rc;
		rc = od_parser_next(&reader->parser, &token);
		switch (rc) {
		case OD_PARSER_SYMBOL:
			/* } */
			if (token.value.num == '}') {
				if (lsc_current->lsc_username == NULL) {
					od_config_reader_error(
						reader, NULL,
						"lsc_username in ldap_storage_credentials '%s' is not defined",
						lsc_current->name);
					goto error;
				}
				if (lsc_current->lsc_password == NULL) {
					od_config_reader_error(
						reader, NULL,
						"lsc_password in ldap_storage_credentials '%s' is not defined",
						lsc_current->name);
					goto error;
				}
				return OK_RESPONSE;
			}
			/* fall through */
		case OD_PARSER_KEYWORD:
			break;
		default:
			od_config_reader_error(reader, &token,
					       "unexpected symbol or token");
			goto error;
		}
		od_keyword_t *keyword;
		keyword = od_keyword_match(od_config_keywords, &token);
		if (keyword == NULL) {
			od_config_reader_error(reader, &token,
					       "unknown parameter");
			return NOT_OK_RESPONSE;
		}

		switch (keyword->id) {
		case OD_LLDAP_STORAGE_USERNAME: {
			if (!od_config_reader_string(
				    reader, &lsc_current->lsc_username))
				goto error;
			if (strlen(lsc_current->lsc_username) == 0) {
				od_config_reader_error(
					reader, &token,
					"lsc_username in ldap_storage_credentials '%s' cannot be empty",
					lsc_current->name);
				goto error;
			}

		} break;
		case OD_LLDAP_STORAGE_PASSWORD: {
			if (!od_config_reader_string(
				    reader, &lsc_current->lsc_password))
				goto error;
			if (strlen(lsc_current->lsc_password) == 0) {
				od_config_reader_error(
					reader, &token,
					"lsc_password in ldap_storage_credentials '%s' cannot be empty",
					lsc_current->name);
				goto error;
			}
		} break;
		}
	}

	return OK_RESPONSE;
error:
	if (lsc_current) {
		od_ldap_storage_credentials_free(lsc_current);
	}
	return NOT_OK_RESPONSE;
}
#endif

static int od_config_reader_rule_settings(od_config_reader_t *reader,
					  od_rule_t *rule,
					  od_extention_t *extentions,
					  od_storage_watchdog_t *watchdog)
{
	for (;;) {
		od_token_t token;
		int rc;
		rc = od_parser_next(&reader->parser, &token);
		switch (rc) {
		case OD_PARSER_KEYWORD:
			break;
		case OD_PARSER_EOF:
			od_config_reader_error(reader, &token,
					       "unexpected end of config file");
			return NOT_OK_RESPONSE;
		case OD_PARSER_SYMBOL:
			/* } */
			if (token.value.num == '}')
				return 0;
			/* fall through */
		default:
			od_config_reader_error(
				reader, &token,
				"incorrect or unexpected parameter");
			return NOT_OK_RESPONSE;
		}
		od_keyword_t *keyword;
		keyword = od_keyword_match(od_config_keywords, &token);
		if (keyword == NULL) {
			od_list_t *i;
			bool token_ok = false;
			od_list_foreach(&extentions->modules->link, i)
			{
				od_module_t *curr_module;
				curr_module =
					od_container_of(i, od_module_t, link);
				rc = curr_module->config_rule_init_cb(
					rule, reader, &token);
				if (rc == OD_MODULE_CB_OK_RETCODE) {
					// do not "break" cycle here - let every module to read
					// this init param
					token_ok = true;
				}
			}
			if (!token_ok) {
				od_config_reader_error(reader, &token,
						       "unknown parameter");
				return NOT_OK_RESPONSE;
			}
			/* continue reading config */
			continue;
		}

		switch (keyword->id) {
		/* authentication */
		case OD_LAUTHENTICATION:
			if (!od_config_reader_string(reader, &rule->auth))
				return NOT_OK_RESPONSE;
#ifndef USE_SCRAM
			if (strcmp(rule->auth, "scram-sha-256") == 0) {
				od_config_reader_error(
					reader, &token,
					"SCRAM auth is not supported in this build, try to recompile");
				return NOT_OK_RESPONSE;
			}
#endif
			break;
		/* auth_common_name */
		case OD_LAUTH_COMMON_NAME: {
			if (od_config_reader_is(reader, OD_PARSER_KEYWORD)) {
				if (!od_config_reader_keyword(
					    reader,
					    &od_config_keywords[OD_LDEFAULT]))
					return NOT_OK_RESPONSE;
				rule->auth_common_name_default = 1;
				break;
			}
			od_rule_auth_t *auth;
			auth = od_rules_auth_add(rule);
			if (auth == NULL)
				return NOT_OK_RESPONSE;
			if (!od_config_reader_string(reader,
						     &auth->common_name))
				return NOT_OK_RESPONSE;
			break;
		}
		/* auth_module */
		case OD_LAUTH_MODULE:
			if (!od_config_reader_string(reader,
						     &rule->auth_module))
				return NOT_OK_RESPONSE;
			break;
#ifdef PAM_FOUND
		/* auth_pam_service */
		case OD_LAUTH_PAM_SERVICE:
			if (!od_config_reader_string(reader,
						     &rule->auth_pam_service))
				return NOT_OK_RESPONSE;
			break;
#endif
		/* auth_query */
		case OD_LAUTH_QUERY:
			if (!od_config_reader_string(reader, &rule->auth_query))
				return NOT_OK_RESPONSE;
			break;
		/* auth_query_db */
		case OD_LAUTH_QUERY_DB:
			if (!od_config_reader_string(reader,
						     &rule->auth_query_db))
				return NOT_OK_RESPONSE;
			break;
		/* auth_query_user */
		case OD_LAUTH_QUERY_USER:
			if (!od_config_reader_string(reader,
						     &rule->auth_query_user))
				return NOT_OK_RESPONSE;
			break;
		/* auth_query_user */
		case OD_LAUTH_PASSWORD_PASSTHROUGH:
			if (!od_config_reader_yes_no(
				    reader, &rule->enable_password_passthrough))
				return NOT_OK_RESPONSE;
			break;
		/* password */
		case OD_LPASSWORD:
			if (!od_config_reader_string(reader, &rule->password))
				return NOT_OK_RESPONSE;
			rule->password_len = strlen(rule->password);
			continue;
		/* role */
		case OD_LROLE: {
			od_token_t token;
			int rc;
			od_keyword_t *keyword;
			rc = od_parser_next(&reader->parser, &token);
			if (rc != OD_PARSER_STRING) {
				od_config_reader_error(
					reader, &token,
					"incorrect or unexpected parameter");
				return NOT_OK_RESPONSE;
			}
			keyword = od_keyword_match(od_role_keywords, &token);
			if (keyword == NULL) {
				od_parser_push(&reader->parser, &token);
				od_config_reader_error(reader, &token,
						       "expected role");
				return NOT_OK_RESPONSE;
			}
			rule->user_role = keyword->id;
			break;
		}
		/* client_max */
		case OD_LCLIENT_MAX:
			if (!od_config_reader_number(reader, &rule->client_max))
				return NOT_OK_RESPONSE;
			rule->client_max_set = 1;
			continue;
		/* client_fwd_error */
		case OD_LCLIENT_FWD_ERROR:
			if (!od_config_reader_yes_no(reader,
						     &rule->client_fwd_error))
				return NOT_OK_RESPONSE;
			continue;
		/* reserve_session_server_connection */
		case OD_LPRESERVE_SESSION_SERVER_CONN:
			if (!od_config_reader_yes_no(
				    reader,
				    &rule->reserve_session_server_connection)) {
				return NOT_OK_RESPONSE;
			}
			continue;
		/* quantiles */
		case OD_LQUANTILES: {
			char *quantiles_str = NULL;
			if (!od_config_reader_string(reader, &quantiles_str)) {
				free(quantiles_str);
				return NOT_OK_RESPONSE;
			}
			if (!od_config_reader_quantiles(
				    reader, quantiles_str, &rule->quantiles,
				    &rule->quantiles_count)) {
				free(quantiles_str);
				return NOT_OK_RESPONSE;
			}
			free(quantiles_str);
		} break;
		/* application_name_add_host */
		case OD_LAPPLICATION_NAME_ADD_HOST:
			if (!od_config_reader_yes_no(
				    reader, &rule->application_name_add_host))
				return NOT_OK_RESPONSE;
			continue;
		/* server_lifetime */
		case OD_LSERVER_LIFETIME: {
			int server_lifetime;
			if (!od_config_reader_number(reader, &server_lifetime))
				return NOT_OK_RESPONSE;
			rule->server_lifetime_us = server_lifetime * 1000000L;
		}
			continue;
		case OD_LMIN_POOL_SIZE:
			if (!od_config_reader_number(reader, &rule->min_pool_size))
				return NOT_OK_RESPONSE;
			continue;
#ifdef LDAP_FOUND
		/* ldap_pool_size */
		case OD_LLDAPPOOL_SIZE:
			if (!od_config_reader_number(reader,
						     &rule->ldap_pool_size))
				return NOT_OK_RESPONSE;
			continue;
		/* ldap_pool_timeout */
		case OD_LLDAPPOOL_TIMEOUT:
			if (!od_config_reader_number(reader,
						     &rule->ldap_pool_timeout))
				return NOT_OK_RESPONSE;
			continue;
		/* ldap_pool_ttl */
		case OD_LLDAPPOOL_TTL:
			if (!od_config_reader_number(reader,
						     &rule->ldap_pool_ttl))
				return NOT_OK_RESPONSE;
			continue;
#endif
		/* pool */
		case OD_LPOOL:
			if (!od_config_reader_string(reader, &rule->pool->type))
				return NOT_OK_RESPONSE;
			continue;
		/* pool routing */
		case OD_LPOOL_ROUTING:
			if (!od_config_reader_string(reader,
						     &rule->pool->routing_type))
				return NOT_OK_RESPONSE;
			continue;
		/* pool_size */
		case OD_LPOOL_SIZE:
			if (!od_config_reader_number(reader, &rule->pool->size))
				return NOT_OK_RESPONSE;
			continue;
		/* pool_timeout */
		case OD_LPOOL_TIMEOUT:
			if (!od_config_reader_number(reader,
						     &rule->pool->timeout))
				return NOT_OK_RESPONSE;
			continue;
		/* pool_ttl */
		case OD_LPOOL_TTL:
			if (!od_config_reader_number(reader, &rule->pool->ttl))
				return NOT_OK_RESPONSE;
			continue;
		/* pool_discard */
		case OD_LPOOL_DISCARD:
			if (!od_config_reader_yes_no(reader,
						     &rule->pool->discard))
				return NOT_OK_RESPONSE;
			continue;
		/* pool_smart_discard */
		case OD_LPOOL_SMART_DISCARD:
			if (!od_config_reader_yes_no(
				    reader, &rule->pool->smart_discard))
				return NOT_OK_RESPONSE;
			continue;
		/* pool_cancel */
		case OD_LPOOL_CANCEL:
			if (!od_config_reader_yes_no(reader,
						     &rule->pool->cancel))
				return NOT_OK_RESPONSE;
			continue;
		/* pool_rollback */
		case OD_LPOOL_ROLLBACK:
			if (!od_config_reader_yes_no(reader,
						     &rule->pool->rollback))
				return NOT_OK_RESPONSE;
			continue;
		case OD_LPOOL_RESERVE_PREPARED_STATEMENT:
			if (!od_config_reader_yes_no(
				    reader,
				    &rule->pool->reserve_prepared_statement))
				return NOT_OK_RESPONSE;
			continue;
		/* pool_client_idle_timeout */
		case OD_LPOOL_CLIENT_IDLE_TIMEOUT:
			if (!od_config_reader_number64(
				    reader, &rule->pool->client_idle_timeout)) {
				return NOT_OK_RESPONSE;
			}
			rule->pool->client_idle_timeout *= interval_usec;
			continue;
		/* pool_idle_in_transaction_timeout */
		case OD_LPOOL_IDLE_IN_TRANSACTION_TIMEOUT:
			if (!od_config_reader_number64(
				    reader,
				    &rule->pool->idle_in_transaction_timeout)) {
				return NOT_OK_RESPONSE;
			}
			rule->pool->idle_in_transaction_timeout *=
				interval_usec;
			continue;
		/* storage */
		case OD_LSTORAGE:
			if (!od_config_reader_string(reader,
						     &rule->storage_name))
				return NOT_OK_RESPONSE;
			continue;
		/* storage_database */
		case OD_LSTORAGE_DB:
			if (!od_config_reader_string(reader, &rule->storage_db))
				return NOT_OK_RESPONSE;
			continue;
		/* storage_user */
		case OD_LSTORAGE_USER:
			if (!od_config_reader_string(reader,
						     &rule->storage_user))
				return NOT_OK_RESPONSE;
			rule->storage_user_len = strlen(rule->storage_user);
			continue;
		/* storage_password */
		case OD_LSTORAGE_PASSWORD:
			if (!od_config_reader_string(reader,
						     &rule->storage_password))
				return NOT_OK_RESPONSE;
			rule->storage_password_len =
				strlen(rule->storage_password);
			continue;
		/* log_debug */
		case OD_LLOG_DEBUG:
			if (!od_config_reader_yes_no(reader, &rule->log_debug))
				return NOT_OK_RESPONSE;
			continue;
		/* log_query */
		case OD_LLOG_QUERY:
			if (!od_config_reader_yes_no(reader, &rule->log_query))
				return NOT_OK_RESPONSE;
			continue;
		case OD_LLDAP_ENDPOINT_NAME: {
#ifdef LDAP_FOUND
			if (!od_config_reader_string(reader,
						     &rule->ldap_endpoint_name))
				return NOT_OK_RESPONSE;
			od_ldap_endpoint_t *le = od_ldap_endpoint_find(
				&reader->rules->ldap_endpoints,
				rule->ldap_endpoint_name);
			if (le == NULL) {
				od_config_reader_error(
					reader, NULL,
					"ldap endpoint %s is unknown",
					rule->ldap_endpoint_name);
				return NOT_OK_RESPONSE;
			}
			rule->ldap_endpoint = le;
			continue;
#else
			od_config_reader_error(
				reader, NULL,
				"ldap is not supported, check if ldap library is available on the system");
			return NOT_OK_RESPONSE;
#endif
		}
		case OD_LLDAP_STORAGE_CREDENTIALS_ATTR: {
#ifdef LDAP_FOUND
			if (!od_config_reader_string(
				    reader,
				    &rule->ldap_storage_credentials_attr)) {
				return NOT_OK_RESPONSE;
			}
			if (rule->ldap_endpoint_name == NULL) {
				od_config_reader_error(
					reader, NULL,
					"ldap_endpoint_name is not defined for rule with ldap_storage_credentials_attr '%s'",
					rule->ldap_storage_credentials_attr);
				return NOT_OK_RESPONSE;
			}
			if (strlen(rule->ldap_storage_credentials_attr) == 0) {
				od_config_reader_error(
					reader, NULL,
					"ldap_storage_credentials_attr cannot be empty for rule with ldap_endpoint_name '%s'",
					rule->ldap_endpoint_name);
				return NOT_OK_RESPONSE;
			}
			continue;
#else
			od_config_reader_error(
				reader, NULL,
				"ldap is not supported, check if ldap library is available on the system");
			return NOT_OK_RESPONSE;
#endif
		}
		case OD_LLDAP_STORAGE_CREDENTIALS: {
#ifdef LDAP_FOUND
			if (od_config_reader_ldap_storage_credentials(
				    reader, rule) != OK_RESPONSE) {
				return NOT_OK_RESPONSE;
			}
			if (rule->ldap_storage_credentials_attr == NULL) {
				od_config_reader_error(
					reader, NULL,
					"ldap_storage_credentials_attr is not defined for rule with ldap_endpoint_name '%s'",
					rule->ldap_endpoint_name);
				return NOT_OK_RESPONSE;
			}
			continue;
#else
			od_config_reader_error(
				reader, NULL,
				"ldap is not supported, check if ldap library is available on the system");
			return NOT_OK_RESPONSE;
#endif
		}
		case OD_LWATCHDOG_LAG_QUERY:
			if (watchdog == NULL) {
				od_config_reader_error(
					reader, NULL,
					"watchdog settings specified for non-watchdog route");
				return NOT_OK_RESPONSE;
			}
			if (!od_config_reader_string(reader,
						     &watchdog->query)) {
				return NOT_OK_RESPONSE;
			}
			continue;
		case OD_LWATCHDOG_LAG_INTERVAL:
			if (watchdog == NULL) {
				od_config_reader_error(
					reader, NULL,
					"watchdog settings specified for non-watchdog route");
				return NOT_OK_RESPONSE;
			}
			if (!od_config_reader_number(reader,
						     &watchdog->interval)) {
				return NOT_OK_RESPONSE;
			}
			continue;
		case OD_LCATCHUP_TIMEOUT:
			if (!od_config_reader_number(reader,
						     &rule->catchup_timeout)) {
				return NOT_OK_RESPONSE;
			}
			continue;
		case OD_LCATCHUP_CHECKS:
			if (!od_config_reader_number(reader,
						     &rule->catchup_checks)) {
				return NOT_OK_RESPONSE;
			}
			continue;
		case OD_LOPTIONS:
			if (od_config_reader_pgoptions(reader, &rule->vars) ==
			    NOT_OK_RESPONSE) {
				return NOT_OK_RESPONSE;
			}
			continue;
		default:
			return NOT_OK_RESPONSE;
		}
	}
	return NOT_OK_RESPONSE;
}

static int od_config_reader_route(od_config_reader_t *reader, char *db_name,
				  int db_name_len, int db_is_default,
				  od_extention_t *extentions)
{
	char *user_name = NULL;
	int user_name_len = 0;
	int user_is_default = 0;

	/* user name or default */
	if (od_config_reader_is(reader, OD_PARSER_STRING)) {
		if (!od_config_reader_string(reader, &user_name))
			return NOT_OK_RESPONSE;
	} else {
		if (!od_config_reader_keyword(reader,
					      &od_config_keywords[OD_LDEFAULT]))
			return NOT_OK_RESPONSE;
		user_is_default = 1;
		user_name = strdup("default_user");
		if (user_name == NULL)
			return NOT_OK_RESPONSE;
	}
	user_name_len = strlen(user_name);

	/* ensure rule does not exists and add new rule */
	od_rule_t *rule;
	rule = od_rules_match(reader->rules, db_name, user_name, db_is_default,
			      user_is_default);
	if (rule) {
		od_errorf(reader->error, "route '%s.%s': is redefined", db_name,
			  user_name);
		free(user_name);
		return NOT_OK_RESPONSE;
	}
	rule = od_rules_add(reader->rules);
	if (rule == NULL) {
		free(user_name);
		return NOT_OK_RESPONSE;
	}
	rule->user_is_default = user_is_default;
	rule->user_name_len = user_name_len;
	rule->user_name = strdup(user_name);
	free(user_name);
	if (rule->user_name == NULL)
		return NOT_OK_RESPONSE;
	rule->db_is_default = db_is_default;
	rule->db_name_len = db_name_len;
	rule->db_name = strdup(db_name);
	if (rule->db_name == NULL)
		return NOT_OK_RESPONSE;

	/* { */
	if (!od_config_reader_symbol(reader, '{'))
		return NOT_OK_RESPONSE;

	/* unreach */
	return od_config_reader_rule_settings(reader, rule, extentions, NULL);
}

static inline int od_config_reader_watchdog(od_config_reader_t *reader,
					    od_storage_watchdog_t *watchdog,
					    od_extention_t *extentions)
{
	watchdog->route_usr = "watchod_int";
	watchdog->route_db = "watchod_int";
	int user_name_len = 0;
	user_name_len = strlen(watchdog->route_usr);

	/* ensure rule does not exists and add new rule */
	od_rule_t *rule;
	rule = od_rules_match(reader->rules, watchdog->route_db,
			      watchdog->route_usr, 0, 0);
	if (rule) {
		od_errorf(reader->error, "route '%s.%s': is redefined",
			  watchdog->route_db, watchdog->route_usr);
		return NOT_OK_RESPONSE;
	}
	rule = od_rules_add(reader->rules);
	if (rule == NULL) {
		return NOT_OK_RESPONSE;
	}
	rule->user_is_default = 0;
	rule->user_name_len = user_name_len;
	rule->user_name = strdup(watchdog->route_usr);
	if (rule->user_name == NULL) {
		return NOT_OK_RESPONSE;
	}
	rule->db_is_default = 0;
	rule->db_name_len = strlen(watchdog->route_db);
	rule->db_name = strdup(watchdog->route_db);
	if (rule->db_name == NULL)
		return NOT_OK_RESPONSE;

	/* { */
	if (!od_config_reader_symbol(reader, '{'))
		return NOT_OK_RESPONSE;

	/* unreach */
	if (od_config_reader_rule_settings(reader, rule, extentions,
					   watchdog) == NOT_OK_RESPONSE) {
		return NOT_OK_RESPONSE;
	}

	// force several settings
	watchdog->storage_db = rule->storage_db;
	watchdog->storage_user = rule->storage_user;
	rule->pool->routing = OD_RULE_POOL_INTERVAL;

	return OK_RESPONSE;
}

#ifdef LDAP_FOUND
static inline od_retcode_t
od_config_reader_ldap_endpoint(od_config_reader_t *reader)
{
	od_ldap_endpoint_t *ldap_current;
	ldap_current = od_ldap_endpoint_alloc();
	if (!ldap_current) {
		goto error;
	}

	/* name */
	if (!od_config_reader_string(reader, &ldap_current->name)) {
		goto error;
	}

	if (od_ldap_endpoint_find(&reader->rules->ldap_endpoints,
				  ldap_current->name) != NULL) {
		od_config_reader_error(reader, NULL,
				       "duplicate ldap endpoint definition: %s",
				       ldap_current->name);
		goto error;
	}

	od_rules_ldap_endpoint_add(reader->rules, ldap_current);

	/* { */
	if (!od_config_reader_symbol(reader, '{')) {
		goto error;
	}

	for (;;) {
		od_token_t token;
		int rc;
		rc = od_parser_next(&reader->parser, &token);
		switch (rc) {
		case OD_PARSER_SYMBOL:
			/* } */
			if (token.value.num == '}') {
				goto init;
			}
			/* fall through */
		case OD_PARSER_KEYWORD:
			break;
		default:
			od_config_reader_error(reader, &token,
					       "unexpected symbol or token");
			goto error;
		}
		od_keyword_t *keyword;
		keyword = od_keyword_match(od_config_keywords, &token);
		if (keyword == NULL) {
			od_config_reader_error(reader, &token,
					       "unknown parameter");
			return NOT_OK_RESPONSE;
		}

		switch (keyword->id) {
		case OD_LLDAP_SERVER: {
			if (!od_config_reader_string(reader,
						     &ldap_current->ldapserver))
				goto error;

		} break;
		case OD_LLDAP_PORT: {
			if (!od_config_reader_number64(reader,
						       &ldap_current->ldapport))
				goto error;

		} break;
		case OD_LLDAP_PREFIX: {
			if (!od_config_reader_string(reader,
						     &ldap_current->ldapprefix))
				goto error;

		} break;
		case OD_LLDAP_SUFFIX: {
			if (!od_config_reader_string(reader,
						     &ldap_current->ldapsuffix))
				goto error;

		} break;
		case OD_LLDAP_SEARCH_ATTRIBUTE: {
			if (!od_config_reader_string(
				    reader, &ldap_current->ldapsearchattribute))
				goto error;

		} break;
		case OD_LLDAP_SCOPE: {
			if (!od_config_reader_string(reader,
						     &ldap_current->ldapscope))
				goto error;

		} break;
		case OD_LLDAP_SCHEME: {
			if (!od_config_reader_string(reader,
						     &ldap_current->ldapscheme))
				goto error;

		} break;
		case OD_LLDAP_BASEDN: {
			if (!od_config_reader_string(reader,
						     &ldap_current->ldapbasedn))
				goto error;

		} break;
		case OD_LLDAP_BINDDN: {
			if (!od_config_reader_string(reader,
						     &ldap_current->ldapbinddn))
				goto error;

		} break;
		case OD_LLDAP_BIND_PASSWD: {
			if (!od_config_reader_string(
				    reader, &ldap_current->ldapbindpasswd))
				goto error;

		} break;
		case OD_LLDAP_SEARCH_FILTER: {
			if (!od_config_reader_string(
				    reader, &ldap_current->ldapsearchfilter))
				goto error;

		} break;
		}
	}

init:
	if (od_ldap_endpoint_prepare(ldap_current) != OK_RESPONSE) {
		od_config_reader_error(reader, NULL,
				       "failed to initialize ldap endpoint");
		goto error;
	}

	/* unreach */
	return OK_RESPONSE;
error:
	if (ldap_current) {
		od_ldap_endpoint_free(ldap_current);
	}
	return NOT_OK_RESPONSE;
}
#endif

static inline od_retcode_t od_config_reader_module(od_config_reader_t *reader,
						   od_extention_t *ext)
{
	char *module_path = NULL;
	int rc;
	rc = od_config_reader_string(reader, &module_path);
	if (rc == -1) {
		return rc;
	}

	od_module_t *module = od_modules_find(ext->modules, module_path);

	if (module != NULL) {
		free(module_path);
		// skip all related conf
		/* { */
		if (!od_config_reader_symbol(reader, '{'))
			return NOT_OK_RESPONSE;

		for (;;) {
			od_token_t token;
			int rc;
			rc = od_parser_next(&reader->parser, &token);
			switch (rc) {
			case OD_PARSER_SYMBOL:
				/* } */
				if (token.value.num == '}') {
					return 0;
				}
				/* fall through */
			default:
				continue;
			}
		}

		return OK_RESPONSE;
	}

	if (od_target_module_add(NULL, ext->modules, module_path) ==
	    OD_MODULE_CB_FAIL_RETCODE) {
		goto error;
	}

	module = od_modules_find(ext->modules, module_path);
	assert(module != NULL);

	if (module->config_module_init_db == NULL) {
		goto error;
	}
	rc = module->config_module_init_db(reader);
	if (rc != OD_MODULE_CB_OK_RETCODE) {
		goto error;
	}

	return OK_RESPONSE;
error:
	free(module_path);
	return NOT_OK_RESPONSE;
}

static int od_config_reader_database(od_config_reader_t *reader,
				     od_extention_t *extentions)
{
	char *db_name = NULL;
	int db_name_len = 0;
	int db_is_default = 0;

	/* name or default */
	if (od_config_reader_is(reader, OD_PARSER_STRING)) {
		if (!od_config_reader_string(reader, &db_name))
			return NOT_OK_RESPONSE;
	} else {
		if (!od_config_reader_keyword(reader,
					      &od_config_keywords[OD_LDEFAULT]))
			return NOT_OK_RESPONSE;
		db_is_default = 1;
		db_name = strdup("default_db");
		if (db_name == NULL)
			return NOT_OK_RESPONSE;
	}
	db_name_len = strlen(db_name);

	/* { */
	if (!od_config_reader_symbol(reader, '{'))
		goto error;

	for (;;) {
		od_token_t token;
		int rc;
		rc = od_parser_next(&reader->parser, &token);
		switch (rc) {
		case OD_PARSER_KEYWORD:
			break;
		case OD_PARSER_EOF:
			od_config_reader_error(reader, &token,
					       "unexpected end of config file");
			goto error;
		case OD_PARSER_SYMBOL:
			/* } */
			if (token.value.num == '}') {
				free(db_name);
				return 0;
			}
			/* fall through */
		default:
			od_config_reader_error(
				reader, &token,
				"incorrect or unexpected parameter");
			goto error;
		}
		od_keyword_t *keyword;
		keyword = od_keyword_match(od_config_keywords, &token);
		if (keyword == NULL) {
			od_config_reader_error(reader, &token,
					       "unknown parameter");
			goto error;
		}
		switch (keyword->id) {
		/* user */
		case OD_LUSER:
			rc = od_config_reader_route(reader, db_name,
						    db_name_len, db_is_default,
						    extentions);
			if (rc == -1)
				goto error;
			continue;
		default:
			od_config_reader_error(reader, &token,
					       "unexpected parameter");
			goto error;
		}
	}
	/* unreach */
	return NOT_OK_RESPONSE;
error:
	free(db_name);
	return NOT_OK_RESPONSE;
}

static int od_config_reader_hba_import(od_config_reader_t *config_reader)
{
	od_config_reader_t reader;
	memset(&reader, 0, sizeof(reader));
	reader.config = config_reader->config;
	reader.error = config_reader->error;
	reader.hba_rules = config_reader->hba_rules;
	int rc;
	rc = od_config_reader_open(&reader, config_reader->config->hba_file);
	if (rc == -1)
		return -1;
	rc = od_hba_reader_parse(&reader);
	od_config_reader_close(&reader);

	return rc;
}

static int od_config_reader_parse(od_config_reader_t *reader,
				  od_extention_t *extentions)
{
	od_config_t *config = reader->config;
	for (;;) {
		od_token_t token;
		int rc;
		rc = od_parser_next(&reader->parser, &token);
		switch (rc) {
		case OD_PARSER_EOF:
			goto success;
		case OD_PARSER_KEYWORD:
			break;
		default:
			od_config_reader_error(
				reader, &token,
				"incorrect or unexpected parameter");
			goto error;
		}

		od_keyword_t *keyword;
		keyword = od_keyword_match(od_config_keywords, &token);
		if (keyword == NULL) {
			od_config_reader_error(reader, &token,
					       "unknown parameter");
			goto error;
		}
		switch (keyword->id) {
		/* include */
		case OD_LINCLUDE: {
			char *config_file = NULL;
			if (!od_config_reader_string(reader, &config_file))
				return NOT_OK_RESPONSE;
			rc = od_config_reader_import(
				reader->config, reader->rules, reader->error,
				extentions, reader->global, reader->hba_rules,
				config_file);
			free(config_file);
			if (rc == -1) {
				goto error;
			}
			continue;
		}
		/* daemonize */
		case OD_LDAEMONIZE:
			if (!od_config_reader_yes_no(reader,
						     &config->daemonize)) {
				goto error;
			}
			continue;
		/* priority */
		case OD_LPRIORITY:
			if (!od_config_reader_number(reader,
						     &config->priority)) {
				goto error;
			}
			continue;
		/* pid_file */
		case OD_LPID_FILE:
			if (!od_config_reader_string(reader,
						     &config->pid_file)) {
				goto error;
			}
			continue;
		/* unix_socket_dir */
		case OD_LUNIX_SOCKET_DIR:
			if (!od_config_reader_string(
				    reader, &config->unix_socket_dir)) {
				goto error;
			}
			continue;
		/* unix_socket_mode */
		case OD_LUNIX_SOCKET_MODE:
			if (!od_config_reader_string(
				    reader, &config->unix_socket_mode)) {
				goto error;
			}
			continue;
		/* locks_dir */
		case OD_LLOCKS_DIR:
			if (!od_config_reader_string(reader,
						     &config->locks_dir)) {
				goto error;
			}
			continue;
		/* enable_online_restart */
		case OD_LENABLE_ONLINE_RESTART:
			if (!od_config_reader_yes_no(
				    reader,
				    &config->enable_online_restart_feature)) {
				goto error;
			}
			continue;
		/* graceful_die_on_errors */
		case OD_LGRACEFUL_DIE_ON_ERRORS:
			if (!od_config_reader_yes_no(
				    reader, &config->graceful_die_on_errors)) {
				goto error;
			}
			continue;
		case OD_LBINDWITH_REUSEPORT:
			if (!od_config_reader_yes_no(
				    reader, &config->bindwith_reuseport)) {
				goto error;
			}
			continue;
		/* log_debug */
		case OD_LLOG_DEBUG:
			if (!od_config_reader_yes_no(reader,
						     &config->log_debug)) {
				goto error;
			}
			continue;
		/* log_stdout */
		case OD_LLOG_TO_STDOUT:
			if (!od_config_reader_yes_no(reader,
						     &config->log_to_stdout)) {
				goto error;
			}
			continue;
		/* log_config */
		case OD_LLOG_CONFIG:
			if (!od_config_reader_yes_no(reader,
						     &config->log_config)) {
				goto error;
			}
			continue;
		/* log_session */
		case OD_LLOG_SESSION:
			if (!od_config_reader_yes_no(reader,
						     &config->log_session)) {
				goto error;
			}
			continue;
		/* log_query */
		case OD_LLOG_QUERY:
			if (!od_config_reader_yes_no(reader,
						     &config->log_query)) {
				goto error;
			}
			continue;
		/* log_stats */
		case OD_LLOG_STATS:
			if (!od_config_reader_yes_no(reader,
						     &config->log_stats)) {
				goto error;
			}
			continue;
		/* log_format */
		case OD_LLOG_FORMAT:
			if (!od_config_reader_string(reader,
						     &config->log_format)) {
				goto error;
			}
			continue;
		/* log_file */
		case OD_LLOG_FILE:
			if (!od_config_reader_string(reader,
						     &config->log_file)) {
				goto error;
			}
			continue;
		/* log_syslog */
		case OD_LLOG_SYSLOG:
			if (!od_config_reader_yes_no(reader,
						     &config->log_syslog)) {
				goto error;
			}
			continue;
		/* log_syslog_ident */
		case OD_LLOG_SYSLOG_IDENT:
			if (!od_config_reader_string(
				    reader, &config->log_syslog_ident)) {
				goto error;
			}
			continue;
		/* log_syslog_facility */
		case OD_LLOG_SYSLOG_FACILITY:
			if (!od_config_reader_string(
				    reader, &config->log_syslog_facility)) {
				goto error;
			}
			continue;
		/* stats_interval */
		case OD_LSTATS_INTERVAL:
			if (!od_config_reader_number(reader,
						     &config->stats_interval)) {
				goto error;
			}

			continue;
		/* client_max */
		case OD_LCLIENT_MAX:
			if (!od_config_reader_number(reader,
						     &config->client_max)) {
				goto error;
			}
			config->client_max_set = 1;
			continue;
		/* client_max_routing */
		case OD_LCLIENT_MAX_ROUTING:
			if (!od_config_reader_number(
				    reader, &config->client_max_routing)) {
				goto error;
			}
			continue;
		/* server_login_retry */
		case OD_LSERVER_LOGIN_RETRY:
			if (!od_config_reader_number(
				    reader, &config->server_login_retry)) {
				goto error;
			}
			continue;
		/* readahead */
		case OD_LREADAHEAD:
			if (!od_config_reader_number(reader,
						     &config->readahead)) {
				goto error;
			}
			continue;
		/* nodelay */
		case OD_LNODELAY:
			if (!od_config_reader_yes_no(reader,
						     &config->nodelay)) {
				goto error;
			}
			continue;
		/* keepalive */
		case OD_LKEEPALIVE:
			if (!od_config_reader_number(reader,
						     &config->keepalive)) {
				goto error;
			}
			continue;
		/* keepalive_keep_interval */
		case OD_LKEEPALIVE_INTERVAL:
			if (!od_config_reader_number(
				    reader, &config->keepalive_keep_interval)) {
				goto error;
			}
			continue;
		/* keepalive_probes */
		case OD_LKEEPALIVE_PROBES:
			if (!od_config_reader_number(
				    reader, &config->keepalive_probes)) {
				goto error;
			}
			continue;

		/* keepalive_usr_timeout */
		case OD_LKEEPALIVE_USR_TIMEOUT:
			if (!od_config_reader_number(
				    reader, &config->keepalive_usr_timeout)) {
				goto error;
			}
			continue;
		/* log_stats_prom */
		case OD_LLOG_GENERAL_STATS_PROM: {
			if (!od_config_reader_yes_no(
				    reader, &config->log_general_stats_prom))
				goto error;
			continue;
		}
		case OD_LLOG_ROUTE_STATS_PROM: {
			if (!od_config_reader_yes_no(
				    reader, &config->log_route_stats_prom))
				goto error;
			continue;
		}
		case OD_LPROMHTTP_PORT: {
			int port;
			if (!od_config_reader_number(reader, &port))
				goto error;
#ifdef PROMHTTP_FOUND
			if (od_prom_set_port(
				    port, ((od_cron_t *)(reader->global->cron))
						  ->metrics) != OK_RESPONSE)
				goto error;
#endif
			continue;
		}
		/* workers */
		case OD_LWORKERS: {
			od_token_t tok;
			int rc;
			rc = od_parser_next(&reader->parser, &tok);
			switch (rc) {
			case OD_PARSER_NUM: {
				config->workers = tok.value.num;
			} break;
			case OD_PARSER_STRING: {
				if (strncmp(tok.value.string.pointer, "auto",
					    tok.value.string.size) == 0) {
					config->workers =
						(1 + od_get_ncpu()) >> 1;
					break;
				}
			}
			// fall through
			default:
				od_config_reader_error(
					reader, &tok,
					"expected 'number' or '\"auto\"'");
				goto error;
			}
		}
			continue;
		/* resolvers */
		case OD_LRESOLVERS:
			if (!od_config_reader_number(reader,
						     &config->resolvers)) {
				goto error;
			}

			continue;
		/* pipeline */
		case OD_LPIPELINE:
			/* fallthrough */
		case OD_LCACHE:

			/* cache */
			/* fallthrough */

		case OD_LCACHE_CHUNK:

			/* cache_chunk */
			/* fallthrough */

		case OD_LPACKET_WRITE_QUEUE:

			/* packet write queue */
			/* fallthrough */

		case OD_LPACKET_READ_SIZE: {
			/* deprecated */
			int unused;
			if (!od_config_reader_number(reader, &unused)) {
				goto error;
			}
			continue;
		}
		/* cache_msg_gc_size */
		case OD_LCACHE_MSG_GC_SIZE:
			if (!od_config_reader_number(
				    reader, &config->cache_msg_gc_size)) {
				goto error;
			}
			continue;
		/* cache_coroutine */
		case OD_LCACHE_COROUTINE:
			if (!od_config_reader_number(
				    reader, &config->cache_coroutine)) {
				goto error;
			}
			continue;
		/* coroutine_stack_size */
		case OD_LCOROUTINE_STACK_SIZE:
			if (!od_config_reader_number(
				    reader, &config->coroutine_stack_size)) {
				goto error;
			}
			continue;
		/* listen */
		case OD_LLISTEN:
			rc = od_config_reader_listen(reader);
			if (rc == -1) {
				goto error;
			}
			continue;
		/* storage */
		case OD_LSTORAGE:
			rc = od_config_reader_storage(reader, extentions);
			if (rc == -1) {
				goto error;
			}
			continue;
		/* database */
		case OD_LDATABASE:
			rc = od_config_reader_database(reader, extentions);
			if (rc == -1) {
				goto error;
			}
			continue;
			/* ldap service */
		case OD_LLDAP_ENDPOINT: {
#ifdef LDAP_FOUND
			rc = od_config_reader_ldap_endpoint(reader);
			if (rc != OK_RESPONSE) {
				goto error;
			}
			continue;
#else
			od_config_reader_error(reader, &token,
					       "unexpected parameter");
			goto error;

#endif
		}
			/* module */
		case OD_LMODULE: {
			rc = od_config_reader_module(reader, extentions);
			if (rc == -1) {
				goto error;
			}
			continue;
		}
		case OD_LHBA_FILE: {
			rc = od_config_reader_string(reader, &config->hba_file);
			if (rc == -1)
				goto error;
			rc = od_config_reader_hba_import(reader);
			if (rc == -1)
				goto error;
			continue;
		}
		default:
			od_config_reader_error(reader, &token,
					       "unexpected parameter");
			goto error;
		}
	}
	/* unreach */
	return NOT_OK_RESPONSE;
error:
	return NOT_OK_RESPONSE;
success:
	if (!config->client_max_routing) {
		config->client_max_routing = config->workers * 16;
	}

	return 0;
}

int od_config_reader_import(od_config_t *config, od_rules_t *rules,
			    od_error_t *error, od_extention_t *extentions,
			    od_global_t *global, od_hba_rules_t *hba_rules,
			    char *config_file)
{
	od_config_reader_t reader;
	memset(&reader, 0, sizeof(reader));
	reader.error = error;
	reader.config = config;
	reader.rules = rules;
	reader.hba_rules = hba_rules;
	reader.global = global;
	int rc;
	rc = od_config_reader_open(&reader, config_file);
	if (rc == -1) {
		return NOT_OK_RESPONSE;
	}

	rc = od_config_reader_parse(&reader, extentions);
	od_config_reader_close(&reader);

	return rc;
}

void yb_read_conf_from_env_var(od_rules_t *rules, od_config_t *config,
			       od_logger_t *logger)
{
#if YB_POOL_MODE == POOL_PER_DB
	char *yb_username = ("YB_YSQL_CONN_MGR_USER");
	const int yb_username_len = strlen(yb_username);
#endif

	const char *yb_password = getenv("YB_YSQL_CONN_MGR_PASSWORD");

	/* strlen returns 0 if the env var is not set. */
	const int yb_password_len = strlen(yb_password);

	/*
	 * Connections from Ysql Connection Manager will be authenticated
	 * via yb-tserver-key. Therefore, yb_password can't be null.
	 */
	assert(yb_password != NULL);

	od_list_t *i;
	/* rules */
	od_list_foreach(&rules->rules, i)
	{
		od_rule_t *rule;
		rule = od_container_of(i, od_rule_t, link);

#if YB_POOL_MODE == POOL_PER_DB
		/* Set storage_user */
		if (yb_username != NULL) {
			if (rule->storage_user)
				free(rule->storage_user);
			rule->storage_user = (char *)malloc(
				sizeof(char) * (yb_username_len + 1));
			strcpy(rule->storage_user, yb_username);
			rule->storage_user_len = strlen(rule->storage_user);
		}
#endif

		/* Set storage_password */
		if (yb_password != NULL) {
			if (rule->storage_password)
				free(rule->storage_password);
			rule->storage_password = (char *)malloc(
				sizeof(char) * (yb_password_len + 1));
			strcpy(rule->storage_password, yb_password);
			rule->storage_password_len =
				strlen(rule->storage_password);
		}
	}
}
