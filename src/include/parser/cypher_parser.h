#ifndef AG_CYPHER_PARSER_H
#define AG_CYPHER_PARSER_H

#include "nodes/pg_list.h"

List *parse_cypher(const char *s);

#endif
