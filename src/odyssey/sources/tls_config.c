/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <kiwi.h>
#include <machinarium.h>
#include <odyssey.h>

od_tls_opts_t *od_tls_opts_alloc(void)
{
	od_tls_opts_t *opts = malloc(sizeof(od_tls_opts_t));
	if (opts == NULL) {
		return NULL;
	}

	memset(opts, 0, sizeof(od_tls_opts_t));
	return opts;
}

od_retcode_t od_tls_opts_free(od_tls_opts_t *opts)
{
	if (opts->tls) {
		free(opts->tls);
	}

	if (opts->tls_ca_file) {
		free(opts->tls_ca_file);
	}

	if (opts->tls_key_file) {
		free(opts->tls_key_file);
	}

	if (opts->tls_cert_file) {
		free(opts->tls_cert_file);
	}

	if (opts->tls_protocols) {
		free(opts->tls_protocols);
	}

	free(opts);
	return OK_RESPONSE;
}
