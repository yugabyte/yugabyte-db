-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "ALTER EXTENSION age UPDATE TO '1.0.0'" to load this file. \quit

CREATE FUNCTION ag_catalog.load_labels_from_file(graph_name name,
                                            label_name name,
                                            file_path text,
                                            id_field_exists bool default true)
    RETURNS void
    LANGUAGE c
    AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.load_edges_from_file(graph_name name,
                                                label_name name,
                                                file_path text)
    RETURNS void
    LANGUAGE c
    AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog._cypher_merge_clause(internal)
RETURNS void
LANGUAGE c
AS 'MODULE_PATHNAME';
    
CREATE FUNCTION ag_catalog.age_unnest(agtype, block_types boolean = false)
    RETURNS SETOF agtype
    LANGUAGE c
    STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

