#ifndef AG_AG_GRAPH_H
#define AG_AG_GRAPH_H

#include "postgres.h"

Oid insert_graph(const Name graph_name, const Oid nsp_id);
void delete_graph(const Name graph_name);
void update_graph_name(const Name graph_name, const Name new_name);

Oid get_graph_oid(const char *graph_name);
Oid get_graph_namespace(const char *graph_name);

#endif
