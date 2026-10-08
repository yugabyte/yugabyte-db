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

	/* YB: Free up tls fields if set */

	if (opts->yb_tls_max_protocol_version) {
		free(opts->yb_tls_max_protocol_version);
	}

	if (opts->yb_tls_ecdh_curve) {
		free(opts->yb_tls_ecdh_curve);
	}

	if (opts->yb_tls_dh_params_file) {
		free(opts->yb_tls_dh_params_file);
	}

	if (opts->yb_tls_crl_file) {
		free(opts->yb_tls_crl_file);
	}

	if (opts->yb_tls_crl_dir) {
		free(opts->yb_tls_crl_dir);
	}

	if (opts->yb_tls_cipher_list) {
		free(opts->yb_tls_cipher_list);
	}

	if (opts->yb_tls_passphrase_command) {
		free(opts->yb_tls_passphrase_command);
	}

	free(opts);
	return OK_RESPONSE;
}
