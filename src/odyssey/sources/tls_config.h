
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
};

typedef struct od_tls_opts od_tls_opts_t;

od_tls_opts_t *od_tls_opts_alloc(void);
od_retcode_t od_tls_opts_free(od_tls_opts_t *);

#endif /* ODYSSEY_TLS_CONFIG_H */
