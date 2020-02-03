#ifndef AG_AG_LABEL_H
#define AG_AG_LABEL_H

#include "postgres.h"

#define LABEL_KIND_VERTEX 'v'
#define LABEL_KIND_EDGE 'e'

Oid insert_label(const char *label_name, Oid label_graph, int32 label_id,
                 char label_kind, Oid label_relation);

Oid get_label_oid(const char *label_name, Oid label_graph);

#define label_exists(label_name, label_graph) \
    OidIsValid(get_label_oid(label_name, label_graph))

#endif
