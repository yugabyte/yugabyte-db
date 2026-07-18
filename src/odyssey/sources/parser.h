#ifndef ODYSSEY_PARSER_H
#define ODYSSEY_PARSER_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include "ctype.h"

typedef struct od_token od_token_t;
typedef struct od_keyword od_keyword_t;
typedef struct od_parser od_parser_t;

enum { OD_PARSER_EOF,
       OD_PARSER_ERROR,
       OD_PARSER_NUM,
       OD_PARSER_KEYWORD,
       OD_PARSER_SYMBOL,
       OD_PARSER_STRING };

struct od_token {
	int type;
	int line;
	union {
		int64_t num;
		struct {
			char *pointer;
			int size;
		} string;
	} value;
};

struct od_keyword {
	int id;
	char *name;
	int name_len;
};

#define od_keyword(name, token)               \
	{                                     \
		token, name, sizeof(name) - 1 \
	}

struct od_parser {
	char *pos;
	char *end;
	od_token_t backlog[4];
	int backlog_count;
	int line;
};

static inline void od_parser_init(od_parser_t *parser, char *string, int size)
{
	parser->pos = string;
	parser->end = string + size;
	parser->line = 0;
	parser->backlog_count = 0;
}

static inline void od_parser_push(od_parser_t *parser, od_token_t *token)
{
	assert(parser->backlog_count < 4);
	parser->backlog[parser->backlog_count] = *token;
	parser->backlog_count++;
}

static inline int od_parser_next(od_parser_t *parser, od_token_t *token)
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
	/* number */
	int is_negative;
	is_negative = *parser->pos == '-' && (parser->pos + 1 < parser->end) &&
		      isdigit(parser->pos[1]);
	if (is_negative || isdigit(*parser->pos)) {
		token->type = OD_PARSER_NUM;
		token->line = parser->line;
		token->value.num = 0;
		if (is_negative)
			parser->pos++;
		while (parser->pos < parser->end && isdigit(*parser->pos)) {
			token->value.num =
				(token->value.num * 10) + *parser->pos - '0';
			parser->pos++;
		}
		if (is_negative)
			token->value.num *= -1;
		return token->type;
	}
	/* symbols */
	if (*parser->pos != '\"' && ispunct(*parser->pos)) {
		token->type = OD_PARSER_SYMBOL;
		token->line = parser->line;
		token->value.num = *parser->pos;
		parser->pos++;
		return token->type;
	}
	/* keyword */
	if (isalpha(*parser->pos)) {
		token->type = OD_PARSER_KEYWORD;
		token->line = parser->line;
		token->value.string.pointer = parser->pos;
		while (parser->pos < parser->end &&
		       (*parser->pos == '_' || isalpha(*parser->pos) ||
			isdigit(*parser->pos)))
			parser->pos++;
		token->value.string.size =
			parser->pos - token->value.string.pointer;
		return token->type;
	}
	/* string */
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
			if ((*parser->pos == '\\') &&
			    (parser->pos + 1 != parser->end))
				parser->pos += 2;
			else
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

static inline od_keyword_t *od_keyword_match(od_keyword_t *list,
					     od_token_t *token)
{
	od_keyword_t *current = &list[0];
	for (; current->name; current++) {
		if (current->name_len != token->value.string.size)
			continue;
		if (strncasecmp(current->name, token->value.string.pointer,
				token->value.string.size) == 0)
			return current;
	}
	return NULL;
}

static inline void od_token_to_string_dest(od_token_t *token, char *dest)
{
	*dest = '\0';
	strncat(dest, token->value.string.pointer, token->value.string.size);
	return;
}

#endif /* ODYSSEY_PARSER_H */
