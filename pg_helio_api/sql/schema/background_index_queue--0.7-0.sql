CREATE TABLE __API_CATALOG_SCHEMA__.__EXTENSION_OBJECT__(_index_queue)
(
    index_cmd text not null,
    cmd_type char CHECK (cmd_type IN ('C', 'D', 'R')), -- 'C' for CREATE INDEX and 'D' for DROP INDEX, 'R' for REINDEX
    index_id integer not null,
    index_cmd_status integer default 1, -- This gets represented as enum IndexCmdStatus in index.h
    comment text,
    op_id text
);

GRANT SELECT ON TABLE __API_CATALOG_SCHEMA__.__EXTENSION_OBJECT__(_index_queue) TO public;
GRANT ALL ON TABLE __API_CATALOG_SCHEMA__.__EXTENSION_OBJECT__(_index_queue) TO __API_ADMIN_ROLE__;