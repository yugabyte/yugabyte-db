
#ifndef ODYSSEY_TLS_CONFIG_H
#define ODYSSEY_TLS_CONFIG_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

typedef enum {
	OD_CONFIG_TLS_DISABLE,
	OD_CONFIG_TLS_ALLOW,
	OD_CONFIG_TLS_REQUIRE,
	OD_CONFIG_TLS_VERIFY_CA,
	OD_CONFIG_TLS_VERIFY_FULL
} od_config_tls_t;

static inline char *od_config_tls_to_str(od_config_tls_t tls)
{
	switch (tls) {
	case OD_CONFIG_TLS_DISABLE:
		return "disable";
	case OD_CONFIG_TLS_ALLOW:
		return "allow";
	case OD_CONFIG_TLS_REQUIRE:
		return "require";
	case OD_CONFIG_TLS_VERIFY_CA:
		return "verify_ca";
	case OD_CONFIG_TLS_VERIFY_FULL:
		return "verify_full";
	}
	return "UNKNOWN";
}

struct od_tls_opts {
	od_config_tls_t tls_mode;
	char *tls;
	char *tls_ca_file;
	char *tls_key_file;
	char *tls_cert_file;
	char *tls_protocols;
	char *yb_tls_max_protocol_version;
	int yb_tls_prefer_server_ciphers;
	char *yb_tls_ecdh_curve;
	char *yb_tls_dh_params_file;
	/* Path to a PEM-encoded CRL file for client-certificate revocation. */
	char *yb_tls_crl_file;
	/* Directory of hash-named CRL files (openssl rehash format). */
	char *yb_tls_crl_dir;
	/* OpenSSL cipher-list string, e.g. "HIGH:MEDIUM:!aNULL". */
	char *yb_tls_cipher_list;
	/* Command that prints the passphrase for an encrypted tls_key_file. */
	char *yb_tls_passphrase_command;
};

typedef struct od_tls_opts od_tls_opts_t;

od_tls_opts_t *od_tls_opts_alloc(void);
od_retcode_t od_tls_opts_free(od_tls_opts_t *);

#endif /* ODYSSEY_TLS_CONFIG_H */
