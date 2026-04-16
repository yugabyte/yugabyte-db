DROP TABLE IF EXISTS __API_CATALOG_SCHEMA__.__EXTENSION_OBJECT__(_index_queue);

CREATE TABLE __API_CATALOG_SCHEMA__.__EXTENSION_OBJECT__(_index_queue)
(
    index_cmd text not null,
    cmd_type char CHECK (cmd_type IN ('C', 'R')), -- 'C' for CREATE INDEX and 'R' for REINDEX
    index_id integer not null,
    index_cmd_status integer default 1, -- This gets represented as enum IndexCmdStatus in index.h
    global_pid bigint,
    start_time timestamp WITH TIME ZONE,
    collection_id bigint not null,
    comment __CORE_SCHEMA__.bson,
    attempt smallint,
    update_time timestamp with time zone DEFAULT now()
);

CREATE INDEX IF NOT EXISTS __EXTENSION_OBJECT__(_index_queue_indexid_cmdtype) on __API_CATALOG_SCHEMA__.__EXTENSION_OBJECT__(_index_queue) (index_id, cmd_type);
CREATE INDEX IF NOT EXISTS __EXTENSION_OBJECT__(_index_queue_cmdtype_collectionid_cmdstatus) on __API_CATALOG_SCHEMA__.__EXTENSION_OBJECT__(_index_queue) (cmd_type, collection_id, index_cmd_status);

GRANT SELECT ON TABLE __API_CATALOG_SCHEMA__.__EXTENSION_OBJECT__(_index_queue) TO public;
GRANT ALL ON TABLE __API_CATALOG_SCHEMA__.__EXTENSION_OBJECT__(_index_queue) TO __API_ADMIN_ROLE__;