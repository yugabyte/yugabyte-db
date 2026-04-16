#ifndef ODYSSEY_CONFIG_READER_H
#define ODYSSEY_CONFIG_READER_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

extern int od_config_reader_import(od_config_t *, od_rules_t *, od_error_t *,
				   od_extention_t *, od_global_t *,
				   od_hba_rules_t *, char *);

extern void yb_read_conf_from_env_var(od_rules_t *, od_config_t *,
		      od_logger_t *);

#define OD_READER_ERROR_MAX_LEN 1 << 8

static inline void od_config_reader_error(od_config_reader_t *reader,
					  od_token_t *token, char *fmt, ...)
{
	char msg[OD_READER_ERROR_MAX_LEN];
	va_list args;
	va_start(args, fmt);
	od_vsnprintf(msg, sizeof(msg), fmt, args);
	va_end(args);
	int line = reader->parser.line;
	if (token)
		line = token->line;
	od_errorf(reader->error, "%s:%d %s", reader->config_file, line, msg);
}

static inline bool od_config_reader_symbol(od_config_reader_t *reader,
					   char symbol)
{
	od_token_t token;
	int rc;
	rc = od_parser_next(&reader->parser, &token);
	if (rc != OD_PARSER_SYMBOL)
		goto error;
	if (token.value.num != (int64_t)symbol)
		goto error;
	return true;
error:
	od_parser_push(&reader->parser, &token);
	od_config_reader_error(reader, &token, "expected '%c'", symbol);
	return false;
}

bool od_config_reader_keyword(od_config_reader_t *reader,
			      od_keyword_t *keyword);

#endif /* ODYSSEY_CONFIG_READER_H */
