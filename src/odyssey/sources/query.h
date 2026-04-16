#ifndef ODYSSEY_QUERY_H
#define ODYSSEY_QUERY_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

// execute query with (optional) single string param
extern machine_msg_t *od_query_do(od_server_t *server, char *context,
				  char *query, char *param);

__attribute__((hot)) extern int od_query_format(char *format_pos,
						char *format_end,
						kiwi_var_t *user, char *peer,
						char *output, int output_len);

#endif /* ODYSSEY_QUERY_H */
