
/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <kiwi.h>
#include <machinarium.h>
#include <odyssey.h>

machine_msg_t *od_query_do(od_server_t *server, char *context, char *query,
			   char *param)
{
	od_instance_t *instance = server->global->instance;
	od_debug(&instance->logger, context, server->client, server, "%s",
		 query);

	if (od_backend_query_send(server, context, query, param,
				  strlen(query) + 1) == NOT_OK_RESPONSE) {
		return NULL;
	}
	machine_msg_t *ret_msg = NULL;
	machine_msg_t *msg;

	/* wait for response */
	int has_result = 0;
	while (1) {
		msg = od_read(&server->io, UINT32_MAX);
		if (msg == NULL) {
			if (!machine_timedout()) {
				od_error(&instance->logger, context,
					 server->client, server,
					 "read error: %s",
					 od_io_error(&server->io));
			}
			return NULL;
		}

		int save_msg = 0;
		kiwi_be_type_t type;
		type = *(char *)machine_msg_data(msg);

		od_debug(&instance->logger, context, server->client, server,
			 "%s", kiwi_be_type_to_string(type));

		switch (type) {
		case KIWI_BE_ERROR_RESPONSE:
			od_backend_error(server, context, machine_msg_data(msg),
					 machine_msg_size(msg));
			goto error;
		case KIWI_BE_ROW_DESCRIPTION:
			break;
		case KIWI_BE_DATA_ROW: {
			if (has_result) {
				goto error;
			}

			ret_msg = msg;
			has_result = 1;
			save_msg = 1;
			break;
		}
		case KIWI_BE_READY_FOR_QUERY:
			od_backend_ready(server, machine_msg_data(msg),
					 machine_msg_size(msg));

			machine_msg_free(msg);
			return ret_msg;
		default:
			break;
		}

		if (!save_msg) {
			machine_msg_free(msg);
		}
	}
	return ret_msg;
error:
	machine_msg_free(msg);
	return NULL;
}

__attribute__((hot)) int od_query_format(char *format_pos, char *format_end,
					 kiwi_var_t *user, char *peer,
					 char *output, int output_len)
{
	char *dst_pos = output;
	char *dst_end = output + output_len;
	while (format_pos < format_end) {
		if (*format_pos == '%') {
			format_pos++;
			if (od_unlikely(format_pos == format_end))
				break;
			int len;
			switch (*format_pos) {
			case 'u':
				len = od_snprintf(dst_pos, dst_end - dst_pos,
						  "%s", user->value);
				dst_pos += len;
				break;
			case 'h':
				len = od_snprintf(dst_pos, dst_end - dst_pos,
						  "%s", peer);
				dst_pos += len;
				break;
			default:
				if (od_unlikely((dst_end - dst_pos) < 2))
					break;
				dst_pos[0] = '%';
				dst_pos[1] = *format_pos;
				dst_pos += 2;
				break;
			}
		} else {
			if (od_unlikely((dst_end - dst_pos) < 1))
				break;
			dst_pos[0] = *format_pos;
			dst_pos += 1;
		}
		format_pos++;
	}
	if (od_unlikely((dst_end - dst_pos) < 1))
		return -1;
	dst_pos[0] = 0;
	dst_pos++;
	return dst_pos - output;
}
