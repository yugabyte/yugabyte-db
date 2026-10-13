/*
 * YB: Postgres TLS link support for Connection Manager.
 *
 * be-secure-openssl.c is compiled into libmachinarium under YB_CONN_MGR and
 * expects Postgres symbols (GUC storage, AllocateFile, GetConfigOption, …).
 * Conn mgr cannot link the full backend, so those definitions live here.
 *
 * NOTE: this header contains external-linkage definitions, not just
 * declarations, so it must be included from exactly one translation unit
 * (machinarium's tls.c).  A second includer would fail to link with duplicate
 * symbols.
 */

/*
 * be_tls_init() and the Postgres GUC globals below are process-wide, while each
 * machinarium worker thread builds (and caches) its own server SSL_CTX.  Held
 * by mm_tls_get_context() across the whole bind -> be_tls_init() -> take
 * sequence.
 */
pthread_mutex_t yb_pg_tls_link_support_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * Postgres GUC storage, same names/types as be-secure.c.
 * be-secure-openssl.c reads these when compiled into conn mgr.
 */
char	   *ssl_library;
char	   *ssl_cert_file;
char	   *ssl_key_file;
char	   *ssl_ca_file;
char	   *ssl_crl_file;
char	   *ssl_crl_dir;
char	   *ssl_dh_params_file;
char	   *ssl_passphrase_command;
bool		ssl_passphrase_command_supports_reload;
bool		ssl_loaded_verify_locations;
char	   *SSLCipherSuites;
char	   *SSLECDHCurve;
bool		SSLPreferServerCiphers;
int			ssl_min_protocol_version;
int			ssl_max_protocol_version;

/*
 * Same numeric values as enum ssl_protocol_versions in
 * src/postgres/src/include/libpq/libpq.h; string names match
 * ssl_protocol_versions_info in guc.c.  Kept local so machinarium does not
 * include postgres headers.  See the YB note on that enum: keep this in sync.
 */
enum {
	PG_TLS_ANY = 0,
	PG_TLS1_VERSION,
	PG_TLS1_1_VERSION,
	PG_TLS1_2_VERSION,
	PG_TLS1_3_VERSION
};

/*
 * The globals above GUCs are char * because they mirror be-secure.c, where GUC
 * values are writable heap strings. Nothing in be_tls_init() writes through
 * them today, but a future Postgres rebase that does would fault on a .rodata
 * literal. So keeping this a writable string too instead of using a "" literal.
 */
static char ssl_empty_string[] = "";

void yb_pg_tls_report_error(const char *fmt, ...);

static char *mm_tls_pg_str(char *s)
{
	return s ? s : ssl_empty_string;
}

static int yb_mm_tls_protocol_to_pg_enum(const char *protocol)
{
	if (protocol == NULL || protocol[0] == '\0')
		return PG_TLS_ANY;
	if (strcasecmp(protocol, "TLSv1") == 0)
		return PG_TLS1_VERSION;
	if (strcasecmp(protocol, "TLSv1.1") == 0)
		return PG_TLS1_1_VERSION;
	if (strcasecmp(protocol, "TLSv1.2") == 0)
		return PG_TLS1_2_VERSION;
	if (strcasecmp(protocol, "TLSv1.3") == 0)
		return PG_TLS1_3_VERSION;
	return -1;
}

static void yb_mm_tls_bind_pg_ssl_globals(mm_tls_t *tls)
{
	ssl_library = ssl_empty_string;
	ssl_cert_file = mm_tls_pg_str(tls->cert_file);
	ssl_key_file = mm_tls_pg_str(tls->key_file);
	ssl_ca_file = mm_tls_pg_str(tls->ca_file);
	ssl_crl_file = mm_tls_pg_str(tls->yb_crl_file);
	ssl_crl_dir = mm_tls_pg_str(tls->yb_crl_dir);
	ssl_dh_params_file = mm_tls_pg_str(tls->yb_dh_params_file);
	ssl_passphrase_command = mm_tls_pg_str(tls->yb_passphrase_command);
	ssl_passphrase_command_supports_reload = false;
	SSLCipherSuites = mm_tls_pg_str(tls->yb_cipher_list);
	SSLECDHCurve = mm_tls_pg_str(tls->yb_ecdh_curve);
	SSLPreferServerCiphers = tls->yb_prefer_server_ciphers != 0;
	/* Unset min matches Postgres's TLS 1.2 default. */
	ssl_min_protocol_version = tls->protocols ?
		yb_mm_tls_protocol_to_pg_enum(tls->protocols) : PG_TLS1_2_VERSION;
	/* Unset max matches Postgres's "Any" default. */
	ssl_max_protocol_version =
		yb_mm_tls_protocol_to_pg_enum(tls->yb_max_protocol_version);
}

FILE *AllocateFile(const char *name, const char *mode)
{
	return fopen(name, mode);
}

int FreeFile(FILE *file)
{
	return fclose(file);
}

/*
 * Copy of PostgreSQL be-secure-common.c: run_ssl_passphrase_command().
 * Conn mgr cannot link the entire postgres codebase.
 */
int run_ssl_passphrase_command(const char *prompt, bool is_server_start,
			       char *buf, int size)
{
	char command[4096];
	const char *src;
	size_t n = 0;
	FILE *fh;
	size_t len;

	(void)is_server_start;
	if (size <= 0)
		return 0;
	buf[0] = '\0';
	if (ssl_passphrase_command == NULL || ssl_passphrase_command[0] == '\0')
		return 0;
	if (prompt == NULL)
		prompt = "";

	for (src = ssl_passphrase_command; *src; src++) {
		if (src[0] == '%' && src[1] == 'p') {
			size_t plen = strlen(prompt);
			if (n + plen + 1 > sizeof(command))
				goto too_long;
			memcpy(command + n, prompt, plen);
			n += plen;
			src++;
		} else if (src[0] == '%' && src[1] == '%') {
			if (n + 2 > sizeof(command))
				goto too_long;
			command[n++] = '%';
			src++;
		} else {
			if (n + 2 > sizeof(command))
				goto too_long;
			command[n++] = *src;
		}
	}
	command[n] = '\0';

	fh = popen(command, "r");
	if (fh == NULL) {
		yb_pg_tls_report_error(
			"could not execute ssl_passphrase_command");
		return 0;
	}
	if (!fgets(buf, size, fh)) {
		int read_error = ferror(fh);

		memset(buf, 0, (size_t)size);
		pclose(fh);
		if (read_error)
			yb_pg_tls_report_error(
				"could not read from ssl_passphrase_command");
		return 0;
	}
	if (pclose(fh) != 0) {
		memset(buf, 0, (size_t)size);
		yb_pg_tls_report_error("ssl_passphrase_command failed");
		return 0;
	}

	len = strlen(buf);
	while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
		buf[--len] = '\0';
	return (int)len;

too_long:
	yb_pg_tls_report_error("ssl_passphrase_command is too long");
	return 0;
}

/*
 * Copy of PostgreSQL be-secure-common.c: check_ssl_key_file_permissions().
 * Conn mgr cannot link the entire postgres codebase.
 */
bool check_ssl_key_file_permissions(const char *ssl_key_file,
				    bool isServerStart)
{
	struct stat buf;

	(void)isServerStart;
	if (stat(ssl_key_file, &buf) != 0) {
		yb_pg_tls_report_error(
			"could not access private key file \"%s\": %s",
			ssl_key_file, strerror(errno));
		return false;
	}
	if (!S_ISREG(buf.st_mode)) {
		yb_pg_tls_report_error(
			"private key file \"%s\" is not a regular file",
			ssl_key_file);
		return false;
	}
#if !defined(WIN32) && !defined(__CYGWIN__)
	if (buf.st_uid != geteuid() && buf.st_uid != 0) {
		yb_pg_tls_report_error(
			"private key file \"%s\" must be owned by the database user or root",
			ssl_key_file);
		return false;
	}
	if ((buf.st_uid == geteuid() && buf.st_mode & (S_IRWXG | S_IRWXO)) ||
	    (buf.st_uid == 0 && buf.st_mode & (S_IWGRP | S_IXGRP | S_IRWXO))) {
		yb_pg_tls_report_error(
			"private key file \"%s\" has group or world access; "
			"file must have permissions u=rw (0600) or less if owned by the "
			"database user, or permissions u=rw,g=r (0640) or less if owned by root",
			ssl_key_file);
		return false;
	}
#endif
	return true;
}

/*
 * be-secure-openssl.c cannot include machinarium types.  Bind the current
 * mm_io_t around be_tls_init() so ereport() can fill io->tls_error_msg.
 */
static __thread mm_io_t *yb_pg_tls_io;

void yb_pg_tls_set_current_io(mm_io_t *io)
{
	yb_pg_tls_io = io;
}

void yb_pg_tls_report_error(const char *fmt, ...)
{
	va_list args;

	if (yb_pg_tls_io == NULL)
		return;

	va_start(args, fmt);
	mm_vsnprintf(yb_pg_tls_io->tls_error_msg,
		     sizeof(yb_pg_tls_io->tls_error_msg), (char *)fmt, args);
	va_end(args);
	yb_pg_tls_io->tls_error = 1;
	mm_errno_set(EINVAL);
	errno = EINVAL;
}

/*
 * be_tls_init() calls this only to quote the offending value back in the
 * "not supported by this build" message for the two protocol-version GUCs.
 * Anything else has no conn mgr equivalent; returning "" keeps such a message
 * well-formed rather than printing the option name as if it were a value.
 */
const char *
GetConfigOption(const char *name, bool missing_ok, bool restrict_privileged)
{
	(void)missing_ok;
	(void)restrict_privileged;
	if (name == NULL || yb_pg_tls_io == NULL || yb_pg_tls_io->tls == NULL)
		return ssl_empty_string;
	if (strcmp(name, "ssl_min_protocol_version") == 0)
		return mm_tls_pg_str(yb_pg_tls_io->tls->protocols);
	if (strcmp(name, "ssl_max_protocol_version") == 0)
		return mm_tls_pg_str(yb_pg_tls_io->tls->yb_max_protocol_version);
	return ssl_empty_string;
}
