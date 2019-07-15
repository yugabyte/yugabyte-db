#ifndef AG_AG_GRAPH_H
#define AG_AG_GRAPH_H

void insert_graph(const Name graph_name, const Oid nsp_id);
void delete_graph(const Name graph_name);

Oid get_graph_namespace(const Name graph_name);

#endif
