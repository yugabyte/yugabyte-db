
/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <kiwi.h>
#include <machinarium.h>
#include <odyssey.h>

machine_tls_t *od_tls_frontend(od_config_listen_t *config)
{
	int rc;
	machine_tls_t *tls;
	tls = machine_tls_create();
	if (tls == NULL)
		return NULL;

	switch (config->tls_opts->tls_mode) {
	case OD_CONFIG_TLS_ALLOW:
		machine_tls_set_verify(tls, "none");
		break;
	case OD_CONFIG_TLS_REQUIRE:
		machine_tls_set_verify(tls, "peer");
		break;
	default:
		machine_tls_set_verify(tls, "peer_strict");
		break;
	}

	if (config->tls_opts->tls_ca_file) {
		rc = machine_tls_set_ca_file(tls,
					     config->tls_opts->tls_ca_file);
		if (rc == -1) {
			machine_tls_free(tls);
			return NULL;
		}
	}
	if (config->tls_opts->tls_cert_file) {
		rc = machine_tls_set_cert_file(tls,
					       config->tls_opts->tls_cert_file);
		if (rc == -1) {
			machine_tls_free(tls);
			return NULL;
		}
	}
	if (config->tls_opts->tls_key_file) {
		rc = machine_tls_set_key_file(tls,
					      config->tls_opts->tls_key_file);
		if (rc == -1) {
			machine_tls_free(tls);
			return NULL;
		}
	}
	return tls;
}

int od_tls_frontend_accept(od_client_t *client, od_logger_t *logger,
			   od_config_listen_t *config, machine_tls_t *tls)
{
	if (client->startup.is_ssl_request) {
		od_debug(logger, "tls", client, NULL, "ssl request");

		int rc;
		if (config->tls_opts->tls_mode == OD_CONFIG_TLS_DISABLE) {
			/* not supported 'N' */
			machine_msg_t *msg;
			msg = machine_msg_create(sizeof(uint8_t));
			if (msg == NULL)
				return -1;
			uint8_t *type = machine_msg_data(msg);
			*type = 'N';
			rc = od_write(&client->io, msg);
			if (rc == -1) {
				od_error(logger, "tls", client, NULL,
					 "write error: %s",
					 od_io_error(&client->io));
				return -1;
			}
			od_debug(logger, "tls", client, NULL,
				 "is disabled, ignoring");
			return 0;
		}

		/* supported 'S' */
		machine_msg_t *msg;
		msg = machine_msg_create(sizeof(uint8_t));
		if (msg == NULL)
			return -1;
		uint8_t *type = machine_msg_data(msg);
		*type = 'S';
		rc = od_write(&client->io, msg);
		if (rc == -1) {
			od_error(logger, "tls", client, NULL, "write error: %s",
				 od_io_error(&client->io));
			return -1;
		}

		if (od_readahead_unread(&client->io.readahead) > 0) {
			od_error(logger, "tls", client, NULL,
				 "extraneous data from client");
			return -1; // prevent possible buffer, protecting against CVE-2021-23214-like attacks
		}

		rc = machine_set_tls(client->io.io, tls,
				     config->client_login_timeout);
		if (rc == -1) {
			od_error(logger, "tls", client, NULL,
				 "error: %s, login time %d us",
				 od_io_error(&client->io),
				 machine_time_us() - client->time_accept);
			return -1;
		}
		od_debug(logger, "tls", client, NULL, "ok");
		return 0;
	}

	/* Client sends cancel request without encryption */
	if (client->startup.is_cancel)
		return 0;

	switch (config->tls_opts->tls_mode) {
	case OD_CONFIG_TLS_DISABLE:
	case OD_CONFIG_TLS_ALLOW:
		break;
	default:
		od_log(logger, "tls", client, NULL, "required, closing");
		od_frontend_error(client, KIWI_PROTOCOL_VIOLATION,
				  "SSL is required");
		return -1;
	}
	return 0;
}

machine_tls_t *od_tls_backend(od_tls_opts_t *opts)
{
	int rc;
	machine_tls_t *tls;
	tls = machine_tls_create();
	if (tls == NULL)
		return NULL;

	switch (opts->tls_mode) {
	case OD_CONFIG_TLS_ALLOW:
		machine_tls_set_verify(tls, "none");
		break;
	case OD_CONFIG_TLS_REQUIRE:
		machine_tls_set_verify(tls, "peer");
		break;
	default:
		machine_tls_set_verify(tls, "peer_strict");
		break;
	}

	if (opts->tls_ca_file) {
		rc = machine_tls_set_ca_file(tls, opts->tls_ca_file);
		if (rc == -1) {
			machine_tls_free(tls);
			return NULL;
		}
	}
	if (opts->tls_cert_file) {
		rc = machine_tls_set_cert_file(tls, opts->tls_cert_file);
		if (rc == -1) {
			machine_tls_free(tls);
			return NULL;
		}
	}
	if (opts->tls_key_file) {
		rc = machine_tls_set_key_file(tls, opts->tls_key_file);
		if (rc == -1) {
			machine_tls_free(tls);
			return NULL;
		}
	}
	return tls;
}

int od_tls_backend_connect(od_server_t *server, od_logger_t *logger,
			   od_tls_opts_t *opts)
{
	od_debug(logger, "tls", NULL, server, "init");

	/* SSL Request */
	machine_msg_t *msg;
	msg = kiwi_fe_write_ssl_request(NULL);
	if (msg == NULL)
		return -1;
	int rc;
	rc = od_write(&server->io, msg);
	if (rc == -1) {
		od_error(logger, "tls", NULL, server, "write error: %s",
			 od_io_error(&server->io));
		return -1;
	}

	/* read server reply */
	char type;
	rc = od_io_read(&server->io, &type, 1, UINT32_MAX);
	if (rc == -1) {
		od_error(logger, "tls", NULL, server, "read error: %s",
			 od_io_error(&server->io));
		return -1;
	}

	switch (type) {
	case 'S':
		/* supported */
		od_debug(logger, "tls", NULL, server, "supported");
		if (od_readahead_unread(&server->io.readahead) > 0) {
			od_error(logger, "tls", NULL, server,
				 "extraneous data from server");
			return -1; // prevent possible buffer, protecting against CVE-2021-23222-like attacks
		}

		rc = machine_set_tls(server->io.io, server->tls, UINT32_MAX);
		if (rc == -1) {
			od_error(logger, "tls", NULL, server, "error: %s",
				 od_io_error(&server->io));
			return -1;
		}
		od_debug(logger, "tls", NULL, server, "ok");
		break;
	case 'N':
		/* not supported */
		if (opts->tls_mode == OD_CONFIG_TLS_ALLOW) {
			od_debug(logger, "tls", NULL, server,
				 "not supported, continue (allow)");
		} else {
			od_error(logger, "tls", NULL, server,
				 "not supported, closing");
			return -1;
		}
		break;
	default:
		od_error(logger, "tls", NULL, server,
			 "unexpected status reply");
		return -1;
	}
	return 0;
}
