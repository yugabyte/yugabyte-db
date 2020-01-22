#ifndef AG_AG_LABEL_H
#define AG_AG_LABEL_H

#include "postgres.h"
typedef enum cypher_label_kind
{
    CYPHER_LABEL_VERTEX,
    CYPHER_LABEL_EDGE
} cypher_label_kind;

bool label_exists(const Name graph_name, const Name label_name,
                  cypher_label_kind label_type);
void create_label(const Name graph_name, const Name label_name,
                  cypher_label_kind label_type);

#endif
