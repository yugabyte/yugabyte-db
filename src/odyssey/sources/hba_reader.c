
/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <arpa/inet.h>
#include <kiwi.h>
#include <machinarium.h>
#include <odyssey.h>

enum { OD_LLOCAL,
       OD_LHOST,
       OD_LHOSTSSL,
       OD_LHOSTNOSSL,
       OD_LALL,
       OD_LSAMEUSER,
       OD_LALLOW,
       OD_LDENY,
};

static od_keyword_t od_hba_keywords[] = {
	/* connection types */
	od_keyword("local", OD_LLOCAL),
	od_keyword("host", OD_LHOST),
	od_keyword("hostssl", OD_LHOSTSSL),
	od_keyword("hostnossl", OD_LHOSTNOSSL),
	/* db/user */
	od_keyword("all", OD_LALL),
	od_keyword("sameuser", OD_LSAMEUSER),
	/* auth type */
	od_keyword("allow", OD_LALLOW),
	od_keyword("trust", OD_LALLOW),
	od_keyword("deny", OD_LDENY),
	od_keyword("reject", OD_LDENY),
};

static void od_hba_reader_error(od_config_reader_t *reader, char *msg)
{
	od_errorf(reader->error, "%s:%d %s", reader->config_file,
		  reader->parser.line, msg);
}

static int od_hba_parser_next(od_parser_t *parser, od_token_t *token)
{
	/* try to use backlog */
	if (parser->backlog_count > 0) {
		*token = parser->backlog[parser->backlog_count - 1];
		parser->backlog_count--;
		return token->type;
	}
	/* skip white spaces and comments */
	for (;;) {
		while (parser->pos < parser->end && isspace(*parser->pos)) {
			if (*parser->pos == '\n')
				parser->line++;
			parser->pos++;
		}
		if (od_unlikely(parser->pos == parser->end)) {
			token->type = OD_PARSER_EOF;
			return token->type;
		}
		if (*parser->pos != '#')
			break;
		while (parser->pos < parser->end && *parser->pos != '\n')
			parser->pos++;
		if (parser->pos == parser->end) {
			token->type = OD_PARSER_EOF;
			return token->type;
		}
		parser->line++;
	}

	/* symbols */
	if (*parser->pos != '\"' && ispunct(*parser->pos)) {
		token->type = OD_PARSER_SYMBOL;
		token->line = parser->line;
		token->value.num = *parser->pos;
		parser->pos++;
		return token->type;
	}

	if (isalnum(*parser->pos)) {
		token->type = OD_PARSER_KEYWORD;
		token->line = parser->line;
		token->value.string.pointer = parser->pos;
		while (parser->pos < parser->end && *parser->pos != ',' &&
		       (isalnum(*parser->pos) || ispunct(*parser->pos)))
			parser->pos++;
		token->value.string.size =
			parser->pos - token->value.string.pointer;
		return token->type;
	}

	if (*parser->pos == '\"') {
		token->type = OD_PARSER_STRING;
		token->line = parser->line;
		parser->pos++;
		token->value.string.pointer = parser->pos;
		while (parser->pos < parser->end && *parser->pos != '\"') {
			if (*parser->pos == '\n') {
				token->type = OD_PARSER_ERROR;
				return token->type;
			}
			parser->pos++;
		}
		if (od_unlikely(parser->pos == parser->end)) {
			token->type = OD_PARSER_ERROR;
			return token->type;
		}
		token->value.string.size =
			parser->pos - token->value.string.pointer;
		parser->pos++;
		return token->type;
	}

	/* error */
	token->type = OD_PARSER_ERROR;
	token->line = parser->line;
	return token->type;
}

static int od_hba_reader_match_string(od_token_t token, char **value)
{
	char *copy = malloc(token.value.string.size + 1);
	if (copy == NULL) {
		return NOT_OK_RESPONSE;
	}
	memcpy(copy, token.value.string.pointer, token.value.string.size);
	copy[token.value.string.size] = 0;
	if (*value)
		free(*value);
	*value = copy;
	return OK_RESPONSE;
}

static int od_hba_reader_value(od_config_reader_t *reader, void **dest)
{
	od_token_t token;
	int rc;
	char *string_value = NULL;
	rc = od_hba_parser_next(&reader->parser, &token);
	switch (rc) {
	case OD_PARSER_EOF:
		return rc;
	case OD_PARSER_KEYWORD: {
		od_keyword_t *match;
		match = od_keyword_match(od_hba_keywords, &token);
		if (match) {
			*dest = match;
			return OD_PARSER_KEYWORD;
		}
		if (od_hba_reader_match_string(token, &string_value) ==
		    OK_RESPONSE) {
			*dest = string_value;
			return OD_PARSER_STRING;
		}
		od_hba_reader_error(reader, "unable to read string");
		return -1;
	}
	case OD_PARSER_STRING:
		if (od_hba_reader_match_string(token, &string_value) ==
		    OK_RESPONSE) {
			*dest = string_value;
			return OD_PARSER_STRING;
		}
		od_hba_reader_error(reader, "unable to read string");
		return -1;
	default:
		od_hba_reader_error(reader, "expected string or keyword");
		return -1;
	}
}

static int od_hba_reader_address(struct sockaddr_storage *dest,
				 const char *addr)
{
	int rc;
	rc = inet_pton(AF_INET, addr, &((struct sockaddr_in *)dest)->sin_addr);
	if (rc > 0) {
		dest->ss_family = AF_INET;
		return 0;
	}
	if (inet_pton(AF_INET6, addr,
		      &((struct sockaddr_in6 *)dest)->sin6_addr) > 0) {
		dest->ss_family = AF_INET6;
		return 0;
	}
	return -1;
}

static inline uint32 od_hba_bswap32(uint32 x)
{
	return ((x << 24) & 0xff000000) | ((x << 8) & 0x00ff0000) |
	       ((x >> 8) & 0x0000ff00) | ((x >> 24) & 0x000000ff);
}

int od_hba_reader_prefix(od_hba_rule_t *hba, char *prefix)
{
	char *end = NULL;
	long len = strtoul(prefix, &end, 10);
	if (*prefix == '\0' || *end != '\0') {
		return -1;
	}
	if (hba->addr.ss_family == AF_INET) {
		if (len > 32)
			return -1;
		struct sockaddr_in *addr = (struct sockaddr_in *)&hba->mask;
		long mask;
		if (len > 0)
			mask = (0xffffffffUL << (32 - (int)len)) & 0xffffffffUL;
		else
			mask = 0;
		addr->sin_addr.s_addr = od_hba_bswap32(mask);
		return 0;
	} else if (hba->addr.ss_family == AF_INET6) {
		if (len > 128)
			return -1;
		struct sockaddr_in6 *addr = (struct sockaddr_in6 *)&hba->mask;
		int i;
		for (i = 0; i < 16; i++) {
			if (len <= 0)
				addr->sin6_addr.s6_addr[i] = 0;
			else if (len >= 8)
				addr->sin6_addr.s6_addr[i] = 0xff;
			else {
				addr->sin6_addr.s6_addr[i] =
					(0xff << (8 - (int)len)) & 0xff;
			}
			len -= 8;
		}
		return 0;
	}

	return -1;
}

static int od_hba_reader_name(od_config_reader_t *reader,
			      struct od_hba_rule_name *name, bool is_db)
{
	od_keyword_t *keyword = NULL;
	int rc;
	void *value = NULL;
	od_token_t token;
	while (1) {
		rc = od_hba_reader_value(reader, &value);
		switch (rc) {
		case OD_PARSER_STRING: {
			struct od_hba_rule_name_item *item =
				od_hba_rule_name_item_add(name);
			item->value = (char *)value;
			break;
		}
		case OD_PARSER_KEYWORD:
			keyword = (od_keyword_t *)value;
			switch (keyword->id) {
			case OD_LALL:
				name->flags |= OD_HBA_NAME_ALL;
				break;
			case OD_LSAMEUSER:
				if (is_db) {
					name->flags |= OD_HBA_NAME_SAMEUSER;
				}
			}
			break;
		default:
			od_hba_reader_error(reader, "expected name or keyword");
			return -1;
		}

		rc = od_hba_parser_next(&reader->parser, &token);
		if (rc == OD_PARSER_SYMBOL && token.value.num == ',') {
			continue;
		}

		od_parser_push(&reader->parser, &token);
		return 0;
	}
}

int od_hba_reader_parse(od_config_reader_t *reader)
{
	od_hba_rule_t *hba = NULL;

	for (;;) {
		hba = od_hba_rule_create();
		if (hba == NULL) {
			od_hba_reader_error(reader, "memory allocation error");
			return -1;
		}

		/* connection type */
		od_keyword_t *keyword = NULL;
		void *connection_type = NULL;
		od_hba_rule_conn_type_t conn_type;
		int rc;
		rc = od_hba_reader_value(reader, &connection_type);
		if (rc == OD_PARSER_EOF) {
			return 0;
		}
		if (rc != OD_PARSER_KEYWORD) {
			od_hba_reader_error(reader, "invalid connection type");
			goto error;
		}
		keyword = (od_keyword_t *)connection_type;
		switch (keyword->id) {
		case OD_LLOCAL:
			conn_type = OD_CONFIG_HBA_LOCAL;
			break;
		case OD_LHOST:
			conn_type = OD_CONFIG_HBA_HOST;
			break;
		case OD_LHOSTSSL:
			conn_type = OD_CONFIG_HBA_HOSTSSL;
			break;
		case OD_LHOSTNOSSL:
			conn_type = OD_CONFIG_HBA_HOSTNOSSL;
			break;
		default:
			od_hba_reader_error(reader, "invalid connection type");
			goto error;
		}
		hba->connection_type = conn_type;

		/* db & user name */
		if (od_hba_reader_name(reader, &hba->database, true) != 0) {
			goto error;
		}
		if (od_hba_reader_name(reader, &hba->user, false) != 0) {
			goto error;
		}

		if (conn_type != OD_CONFIG_HBA_LOCAL) {
			void *address = NULL;
			char *mask = NULL;

			/* ip address */
			rc = od_hba_reader_value(reader, &address);
			if (rc != OD_PARSER_STRING) {
				od_hba_reader_error(reader,
						    "expected IP address");
				goto error;
			}
			mask = strchr(address, '/');
			if (mask)
				*mask++ = 0;

			if (od_hba_reader_address(&hba->addr, address) ==
			    NOT_OK_RESPONSE) {
				od_hba_reader_error(reader,
						    "invalid IP address");
				goto error;
			}

			/* network mask */
			if (mask) {
				if (od_hba_reader_prefix(hba, mask) == -1) {
					od_hba_reader_error(
						reader,
						"invalid network prefix length");
					goto error;
				}

			} else {
				rc = od_hba_reader_value(reader, &address);
				if (rc != OD_PARSER_STRING) {
					od_hba_reader_error(
						reader,
						"expected network mask");
					goto error;
				}
				if (od_hba_reader_address(&hba->mask,
							  address) == -1) {
					od_hba_reader_error(
						reader, "invalid network mask");
					goto error;
				}
			}
		}

		/* auth method */
		void *auth_method = NULL;
		rc = od_hba_reader_value(reader, &auth_method);
		if (rc != OD_PARSER_KEYWORD) {
			od_hba_reader_error(reader, "expected auth method");
			goto error;
		}

		keyword = (od_keyword_t *)auth_method;
		switch (keyword->id) {
		case OD_LALLOW:
			hba->auth_method = OD_CONFIG_HBA_ALLOW;
			break;
		case OD_LDENY:
			hba->auth_method = OD_CONFIG_HBA_DENY;
			break;
		default:
			od_hba_reader_error(
				reader,
				"invalid auth method: only allow/deny or trust/reject is now supported");
			goto error;
		}

		od_hba_rules_add(reader->hba_rules, hba);
	}
error:
	od_hba_rule_free(hba);
	return -1;
}
