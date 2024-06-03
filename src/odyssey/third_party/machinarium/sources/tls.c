
/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

#include <machinarium.h>
#include <machinarium_private.h>

#if !USE_BORINGSSL && (OPENSSL_VERSION_NUMBER < 0x10100000L)

static pthread_mutex_t *mm_tls_locks = NULL;

static void mm_tls_lock_thread_id(CRYPTO_THREADID *tid)
{
	CRYPTO_THREADID_set_numeric(tid, (unsigned long)pthread_self());
}

static void mm_tls_lock_callback(int mode, int type, const char *file, int line)
{
	(void)file;
	(void)line;
	if (mode & CRYPTO_LOCK)
		pthread_mutex_lock(&mm_tls_locks[type]);
	else
		pthread_mutex_unlock(&mm_tls_locks[type]);
}

static void mm_tls_lock_init(void)
{
	int size = CRYPTO_num_locks() * sizeof(pthread_mutex_t);
	mm_tls_locks = OPENSSL_malloc(size);
	if (mm_tls_locks == NULL) {
		abort();
		return;
	}
	int i = 0;
	for (; i < CRYPTO_num_locks(); i++)
		pthread_mutex_init(&mm_tls_locks[i], NULL);
	CRYPTO_THREADID_set_callback(mm_tls_lock_thread_id);
	CRYPTO_set_locking_callback(mm_tls_lock_callback);
}

static void mm_tls_lock_free(void)
{
	CRYPTO_set_locking_callback(NULL);
	int i = 0;
	for (; i < CRYPTO_num_locks(); i++)
		pthread_mutex_destroy(&mm_tls_locks[i]);
	OPENSSL_free(mm_tls_locks);
}

#endif

void mm_tls_engine_init(void)
{
	SSL_library_init();
	SSL_load_error_strings();
#if !USE_BORINGSSL && (OPENSSL_VERSION_NUMBER < 0x10100000L)
	mm_tls_lock_init();
#endif
}

void mm_tls_engine_free(void)
{
#if !USE_BORINGSSL && (OPENSSL_VERSION_NUMBER < 0x10100000L)
	mm_tls_lock_free();
	ERR_remove_state(getpid());
#endif
	/*
	SSL_COMP_free_compression_methods();
	*/
	/*FIPS_mode_set(0);*/
#if !USE_BORINGSSL
	ENGINE_cleanup();
	CONF_modules_unload(1);
#endif
	EVP_cleanup();
	CRYPTO_cleanup_all_ex_data();
	ERR_free_strings();
}

void mm_tls_init(mm_io_t *io)
{
	(void)io;
}

void mm_tls_free(mm_io_t *io)
{
	if (io->tls_ssl)
		SSL_free(io->tls_ssl);
}

void mm_tls_error_reset(mm_io_t *io)
{
	mm_errno_set(0);
	io->tls_error = 0;
	io->tls_error_msg[0] = 0;
}

static inline char *mm_tls_strerror(int error)
{
	switch (error) {
	case SSL_ERROR_NONE:
		return "SSL_ERROR_NONE";
	case SSL_ERROR_SSL:
		return "SSL_ERROR_SSL";
	case SSL_ERROR_WANT_CONNECT:
		return "SSL_ERROR_CONNECT";
	case SSL_ERROR_WANT_ACCEPT:
		return "SSL_ERROR_ACCEPT";
	case SSL_ERROR_WANT_READ:
		return "SSL_ERROR_WANT_READ";
	case SSL_ERROR_WANT_WRITE:
		return "SSL_ERROR_WANT_WRITE";
	case SSL_ERROR_WANT_X509_LOOKUP:
		return "SSL_ERROR_WANT_X509_LOOKUP";
	case SSL_ERROR_SYSCALL:
		return "SSL_ERROR_SYSCALL";
	case SSL_ERROR_ZERO_RETURN:
		return "SSL_ERROR_ZERO_RETURN";
	}
	return "SSL_ERROR unknown";
}

static inline void mm_tls_error(mm_io_t *io, int ssl_rc, char *fmt, ...)
{
	/* get error description */
	unsigned int error;
	error = SSL_get_error(io->tls_ssl, ssl_rc);
	switch (error) {
	case SSL_ERROR_NONE:
	case SSL_ERROR_ZERO_RETURN:
		/* basically this means connection reset */
		break;
	}
	unsigned int error_peek;
	char *error_str;
	error_str = "unknown error";
	error_peek = ERR_get_error();
	if (error_peek != 0) {
		error_str = ERR_error_string(error_peek, NULL);
	} else if (ssl_rc <= 0) {
		error_str = strerror(mm_errno_get());
	}

	/* error message */
	va_list args;
	va_start(args, fmt);
	int len = 0;
	len = mm_vsnprintf(io->tls_error_msg, sizeof(io->tls_error_msg), fmt,
			   args);
	va_end(args);
	len += mm_snprintf(io->tls_error_msg + len,
			   sizeof(io->tls_error_msg) - len, ": %s: %s",
			   mm_tls_strerror(error), error_str);
	io->tls_error = 1;

	if (errno == 0)
		errno = EIO;
}

SSL_CTX *mm_tls_get_context(mm_io_t *io, int is_client)
{
	mm_tls_ctx_t *ctx_container;
	if (is_client)
		ctx_container = mm_self->client_tls_ctx;
	else
		ctx_container = mm_self->server_tls_ctx;
	while (ctx_container != NULL) {
		if (ctx_container->key == io->tls) {
			return ctx_container->tls_ctx;
		}
		ctx_container = ctx_container->next;
	}
	// Cached context not found - we must create ctx

	SSL_CTX *ctx;
	SSL_METHOD *ssl_method = NULL;
	if (is_client)
		ssl_method = (SSL_METHOD *)SSLv23_client_method();
	else
		ssl_method = (SSL_METHOD *)SSLv23_server_method();
	ctx = SSL_CTX_new(ssl_method);
	if (ctx == NULL) {
		return NULL;
	}

	SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2);
	SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv3);

	SSL_CTX_set_mode(ctx, SSL_MODE_ENABLE_PARTIAL_WRITE);
	SSL_CTX_set_mode(ctx, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
	SSL_CTX_set_mode(ctx, SSL_MODE_RELEASE_BUFFERS);

	/* verify mode */
	int verify = 0;
	switch (io->tls->verify) {
	case MM_TLS_NONE:
		verify = SSL_VERIFY_NONE;
		break;
	case MM_TLS_PEER:
		verify = SSL_VERIFY_PEER;
		break;
	case MM_TLS_PEER_STRICT:
		verify = SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
		break;
	}
	SSL_CTX_set_verify(ctx, verify, NULL);
	SSL_CTX_set_verify_depth(ctx, 6);

	/* cert file */
	int rc;
	if (io->tls->cert_file) {
		rc = SSL_CTX_use_certificate_chain_file(ctx,
							io->tls->cert_file);
		if (!rc) {
			mm_tls_error(io, 0,
				     "SSL_CTX_use_certificate_chain_file()");
			goto error;
		}
	}
	/* key file */
	if (io->tls->key_file) {
		rc = SSL_CTX_use_PrivateKey_file(ctx, io->tls->key_file,
						 SSL_FILETYPE_PEM);
		if (rc != 1) {
			mm_tls_error(io, 0, "SSL_CTX_use_PrivateKey_file()");
			goto error;
		}
	}
	if (io->tls->cert_file && io->tls->key_file) {
		rc = SSL_CTX_check_private_key(ctx);
		if (rc != 1) {
			mm_tls_error(io, 0, "SSL_CTX_check_private_key()");
			goto error;
		}
	}

	/* ca file and ca_path */
	if (io->tls->ca_file || io->tls->ca_path) {
		rc = SSL_CTX_load_verify_locations(ctx, io->tls->ca_file,
						   io->tls->ca_path);
		if (rc != 1) {
			mm_tls_error(io, 0, "SSL_CTX_load_verify_locations()");
			goto error;
		}
	}

	/* ocsp */

	/* set ciphers */
	const char cipher_list[] = "ALL:!aNULL:!eNULL";
	rc = SSL_CTX_set_cipher_list(ctx, cipher_list);
	if (rc != 1) {
		mm_tls_error(io, 0, "SSL_CTX_set_cipher_list()");
		goto error;
	}
	if (!is_client) {
		unsigned char sid[SSL_MAX_SSL_SESSION_ID_LENGTH];
		if (!RAND_bytes(sid, sizeof(sid))) {
			mm_tls_error(io, 0, "failed to generate session id");
			goto error;
		}
		if (!SSL_CTX_set_session_id_context(ctx, sid, sizeof(sid))) {
			mm_tls_error(io, 0, "failed to set session id context");
			goto error;
		}

		SSL_CTX_set_options(ctx, SSL_OP_CIPHER_SERVER_PREFERENCE);
	}
	// Place new ctx on top of cache

	ctx_container = malloc(sizeof(*ctx_container));
	ctx_container->key = io->tls;
	ctx_container->tls_ctx = ctx;

	if (is_client) {
		ctx_container->next = mm_self->client_tls_ctx;
		mm_self->client_tls_ctx = ctx_container;
	} else {
		ctx_container->next = mm_self->server_tls_ctx;
		mm_self->server_tls_ctx = ctx_container;
	}

	return ctx;
error:
	if (ctx)
		SSL_CTX_free(ctx);
	return NULL;
}

static int mm_tls_prepare(mm_io_t *io, int is_client)
{
	SSL *ssl = NULL;
	int rc;

	SSL_CTX *ctx = mm_tls_get_context(io, is_client);
	if (ctx == NULL) {
		goto error;
	}

	ssl = SSL_new(ctx);
	if (ssl == NULL) {
		mm_tls_error(io, 0, "SSL_new()");
		goto error;
	}

	/* set server name */
	if (io->tls->server) {
		rc = SSL_set_tlsext_host_name(ssl, io->tls->server);
		if (rc != 1) {
			mm_tls_error(io, 0, "SSL_set_tlsext_host_name()");
			goto error;
		}
	}

	/* set socket */
	rc = SSL_set_rfd(ssl, io->fd);
	if (rc == -1) {
		mm_tls_error(io, 0, "SSL_set_rfd()");
		goto error;
	}
	rc = SSL_set_wfd(ssl, io->fd);
	if (rc == -1) {
		mm_tls_error(io, 0, "SSL_set_wfd()");
		goto error;
	}

	io->tls_ssl = ssl;
	return 0;
error:
	if (ssl)
		SSL_free(ssl);
	return -1;
}

static inline int mm_tls_verify_name(char *cert_name, const char *name)
{
	char *cert_domain, *domain, *next_dot;
	if (strcasecmp(cert_name, name) == 0)
		return 0;

	/* wildcard match */
	if (cert_name[0] != '*')
		return -1;

	/*
	 * valid wildcards:
	 * - "*.domain.tld"
	 * - "*.sub.domain.tld"
	 * - etc.
	 * reject "*.tld".
	 * no attempt to prevent the use of eg. "*.co.uk".
	 */
	cert_domain = &cert_name[1];
	/* disallow "*"  */
	if (cert_domain[0] == '\0')
		return -1;
	/* disallow "*foo" */
	if (cert_domain[0] != '.')
		return -1;
	/* disallow "*.." */
	if (cert_domain[1] == '.')
		return -1;

	next_dot = strchr(&cert_domain[1], '.');
	/* disallow "*.bar" */
	if (next_dot == NULL)
		return -1;
	/* disallow "*.bar.." */
	if (next_dot[1] == '.')
		return -1;

	domain = strchr(name, '.');
	/* no wildcard match against a name with no host part. */
	if (name[0] == '.')
		return -1;
	/* no wildcard match against a name with no domain part. */
	if (domain == NULL || strlen(domain) == 1)
		return -1;

	if (strcasecmp(cert_domain, domain) == 0)
		return 0;

	return -1;
}

int mm_tls_verify_common_name(mm_io_t *io, char *name)
{
	X509 *cert = NULL;
	X509_NAME *subject_name = NULL;
	char *common_name = NULL;
	int common_name_len = 0;

	cert = SSL_get_peer_certificate(io->tls_ssl);
	if (cert == NULL) {
		mm_tls_error(io, 0, "SSL_get_peer_certificate()");
		return -1;
	}
	subject_name = X509_get_subject_name(cert);
	if (subject_name == NULL) {
		mm_tls_error(io, 0, "X509_get_subject_name()");
		goto error;
	}
	common_name_len = X509_NAME_get_text_by_NID(subject_name,
						    NID_commonName, NULL, 0);
	if (common_name_len < 0) {
		mm_tls_error(io, 0, "X509_NAME_get_text_by_NID()");
		goto error;
	}
	common_name = calloc(common_name_len + 1, 1);
	if (common_name == NULL) {
		mm_tls_error(io, 0, "memory allocation failed");
		goto error;
	}
	X509_NAME_get_text_by_NID(subject_name, NID_commonName, common_name,
				  common_name_len + 1);
	/* validate name */
	if (common_name_len != (int)strlen(common_name)) {
		mm_tls_error(
			io, 0,
			"NUL byte in Common Name field, probably a malicious "
			"server certificate");
		goto error;
	}
	if (mm_tls_verify_name(common_name, name) == -1) {
		mm_tls_error(io, 0, "bad common name: %s (expected %s)",
			     common_name, name);
		goto error;
	}
	X509_free(cert);
	free(common_name);
	return 0;

error:
	X509_free(cert);
	if (common_name)
		free(common_name);
	return -1;
}

static void mm_tls_handshake_cb(mm_fd_t *handle)
{
	mm_machine_t *machine = mm_self;
	mm_io_t *io = handle->on_write_arg;
	mm_call_t *call = &io->call;
	if (mm_call_is_aborted(call))
		return;
	int rc = -1;
	if (io->accepted)
		rc = SSL_accept(io->tls_ssl);
	else if (io->connected)
		rc = SSL_connect(io->tls_ssl);
	if (rc <= 0) {
		int error = SSL_get_error(io->tls_ssl, rc);
		if (error == SSL_ERROR_WANT_READ ||
		    error == SSL_ERROR_WANT_WRITE)
			return;
		if (io->connected)
			mm_tls_error(io, rc, "SSL_connect()");
		else
			mm_tls_error(io, rc, "SSL_accept()");
		call->status = -1;
		goto done;
	}
	/* success */
	call->status = 0;
done:
	mm_loop_read_write_stop(&machine->loop, &io->handle);
	mm_scheduler_wakeup(&mm_self->scheduler, call->coroutine);
}

int mm_tls_handshake(mm_io_t *io, uint32_t timeout)
{
	mm_machine_t *machine = mm_self;
	mm_tls_error_reset(io);

	int is_client = !io->accepted;
	int rc;
	rc = mm_tls_prepare(io, is_client);
	if (rc == -1)
		return -1;

	/* subscribe for connect or accept event */
	rc = mm_loop_read_write(&machine->loop, &io->handle,
				mm_tls_handshake_cb, io);
	if (rc == -1) {
		mm_errno_set(errno);
		return -1;
	}

	/* wait for completion */
	mm_call(&io->call, MM_CALL_HANDSHAKE, timeout);

	rc = mm_loop_read_write_stop(&machine->loop, &io->handle);
	if (rc == -1) {
		mm_errno_set(errno);
		return -1;
	}

	if (io->call.status != 0)
		return -1;

	if (is_client) {
		if (io->tls->server) {
			rc = mm_tls_verify_common_name(io, io->tls->server);
			if (rc == -1)
				return -1;
		}

		rc = SSL_get_verify_result(io->tls_ssl);
		if (rc != X509_V_OK) {
			switch (rc) {
			case 2:
				mm_tls_error(
					io, 0,
					"SSL_get_verify_result(): unable to get issuer certificate");
				break;
			case 3:
				mm_tls_error(
					io, 0,
					"SSL_get_verify_result(): unable to get certificate CRL");
				break;
			case 4:
				mm_tls_error(
					io, 0,
					"SSL_get_verify_result(): unable to decrypt certificate's signature");
				break;
			case 5:
				mm_tls_error(
					io, 0,
					"SSL_get_verify_result(): unable to decrypt CRL's signature");
				break;
			case 6:
				mm_tls_error(
					io, 0,
					"SSL_get_verify_result(): unable to decode issuer public key");
				break;
			case 7:
				mm_tls_error(
					io, 0,
					"SSL_get_verify_result(): certificate signature failure");
				break;
			case 8:
				mm_tls_error(
					io, 0,
					"SSL_get_verify_result(): CRL signature failure");
				break;
			case 9:
				mm_tls_error(
					io, 0,
					"SSL_get_verify_result(): certificate is not yet valid");
				break;
			case 10:
				mm_tls_error(
					io, 0,
					"SSL_get_verify_result(): certificate has expired");
				break;
			case 11:
				mm_tls_error(
					io, 0,
					"SSL_get_verify_result(): CRL is not yet valid");
				break;
			case 12:
				mm_tls_error(
					io, 0,
					"SSL_get_verify_result(): CRL has expired");
				break;
			case 13:
				mm_tls_error(
					io, 0,
					"SSL_get_verify_result(): format error in certificate's notBefore field");
				break;
			case 14:
				mm_tls_error(
					io, 0,
					"SSL_get_verify_result(): format error in certificate's notAfter field");
				break;
			case 15:
				mm_tls_error(
					io, 0,
					"SSL_get_verify_result(): format error in CRL's lastUpdate field");
				break;
			case 16:
				mm_tls_error(
					io, 0,
					"SSL_get_verify_result(): format error in CRL's nextUpdate field");
				break;
			case 17:
				mm_tls_error(
					io, 0,
					"SSL_get_verify_result(): out of memory");
				break;
			case 18:
				mm_tls_error(
					io, 0,
					"SSL_get_verify_result(): self signed certificate");
				break;
			case 19:
				mm_tls_error(
					io, 0,
					"SSL_get_verify_result(): self signed certificate in certificate chain");
				break;
			case 20:
				mm_tls_error(
					io, 0,
					"SSL_get_verify_result(): unable to get local issuer certificate");
				break;
			case 21:
				mm_tls_error(
					io, 0,
					"SSL_get_verify_result(): unable to verify the first certificate");
				break;
			case 22:
				mm_tls_error(
					io, 0,
					"SSL_get_verify_result(): certificate chain too long");
				break;
			case 23:
				mm_tls_error(
					io, 0,
					"SSL_get_verify_result(): certificate revoked");
				break;
			case 24:
				mm_tls_error(
					io, 0,
					"SSL_get_verify_result(): invalid CA certificate");
				break;
			case 25:
				mm_tls_error(
					io, 0,
					"SSL_get_verify_result(): path length constraint exceeded");
				break;
			case 26:
				mm_tls_error(
					io, 0,
					"SSL_get_verify_result(): unsupported certificate purpose");
				break;
			case 27:
				mm_tls_error(
					io, 0,
					"SSL_get_verify_result(): certificate not trusted");
				break;
			case 28:
				mm_tls_error(
					io, 0,
					"SSL_get_verify_result(): certificate rejected");
				break;
			case 29:
				mm_tls_error(
					io, 0,
					"SSL_get_verify_result(): subject issuer mismatch");
				break;
			case 30:
				mm_tls_error(
					io, 0,
					"SSL_get_verify_result(): authority and subject key identifier mismatch");
				break;
			case 31:
				mm_tls_error(
					io, 0,
					"SSL_get_verify_result(): authority and issuer serial number mismatch");
				break;
			case 32:
				mm_tls_error(
					io, 0,
					"SSL_get_verify_result(): key usage does not include certificate signing");
				break;
			case 50:
				mm_tls_error(
					io, 0,
					"SSL_get_verify_result(): application verification failure");
				break;
			default:
				mm_tls_error(
					io, 0,
					"SSL_get_verify_result(): unknown");
			}
			return -1;
		}
	}
	return 0;
}

int mm_tls_write(mm_io_t *io, char *buf, int size)
{
	mm_tls_error_reset(io);
	int rc;
	rc = SSL_write(io->tls_ssl, buf, size);
	if (rc > 0)
		return rc;
	int error = SSL_get_error(io->tls_ssl, rc);
	if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
		errno = EAGAIN;
		return -1;
	}
	mm_tls_error(io, rc, "SSL_write()");
	return -1;
}

int mm_tls_writev(mm_io_t *io, struct iovec *iov, int n)
{
	mm_tls_error_reset(io);

	int size = mm_iov_size_of(iov, n);
	char *buffer = malloc(size);
	if (buffer == NULL) {
		errno = ENOMEM;
		return -1;
	}
	mm_iovcpy(buffer, iov, n);

	int rc;
	rc = SSL_write(io->tls_ssl, buffer, size);
	free(buffer);
	if (rc > 0)
		return rc;
	int error = SSL_get_error(io->tls_ssl, rc);
	if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
		errno = EAGAIN;
		return -1;
	}
	mm_tls_error(io, rc, "SSL_write()");
	return -1;
}

int mm_tls_read_pending(mm_io_t *io)
{
	return SSL_pending(io->tls_ssl) > 0;
}

int mm_tls_read(mm_io_t *io, char *buf, int size)
{
	mm_tls_error_reset(io);
	int rc;
	rc = SSL_read(io->tls_ssl, buf, size);
	if (rc > 0) {
		if (mm_tls_read_pending(io)) {
			mm_cond_signal((mm_cond_t *)io->on_read,
				       &mm_self->scheduler);
		}
		return rc;
	}
	int error = SSL_get_error(io->tls_ssl, rc);
	if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
		errno = EAGAIN;
		return -1;
	}
	if (error == SSL_ERROR_ZERO_RETURN)
		return 0;
	mm_tls_error(io, rc, "SSL_read()");
	return -1;
}
