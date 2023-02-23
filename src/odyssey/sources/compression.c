
/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <kiwi.h>
#include <machinarium.h>
#include <odyssey.h>

int od_compression_frontend_setup(od_client_t *client,
				  od_config_listen_t *config,
				  od_logger_t *logger)
{
	kiwi_var_t *compression_var =
		kiwi_vars_get(&client->vars, KIWI_VAR_COMPRESSION);

	if (compression_var == NULL) {
		/* if there is no compression variable in startup packet,
		 * skip compression initialization */
		return 0;
	}

	char *client_compression_algorithms = compression_var->value;
	char compression_algorithm = MM_ZPQ_NO_COMPRESSION;

	/* if compression support is enabled, choose the compression algorithm */
	if (config->compression) {
		compression_algorithm = machine_compression_choose_alg(
			client_compression_algorithms);
	}

	machine_msg_t *msg =
		kiwi_be_write_compression_ack(NULL, compression_algorithm);

	if (msg == NULL)
		return -1;

	int rc = od_write(&client->io, msg);
	if (rc == -1) {
		od_error(logger, "compression", client, NULL, "write error: %s",
			 od_io_error(&client->io));
		return -1;
	}

	if (compression_algorithm == MM_ZPQ_NO_COMPRESSION) {
		/* do not perform the compression initialization
		 * if failed to choose any compression algorithm */
		return 0;
	}

	/* initialize compression */
	rc = machine_set_compression(client->io.io, compression_algorithm);
	if (rc == -1) {
		od_debug(logger, "compression", client, NULL,
			 "failed to initialize compression w/ algorithm %c",
			 compression_algorithm);
		return -1;
	}

	return 0;
}
