DROP TABLE IF EXISTS helio_api_catalog.helio_index_queue;

CREATE TABLE helio_api_catalog.helio_index_queue
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
    update_time timestamp with time zone DEFAULT now(),
    user_oid Oid CHECK (user_oid IS NULL OR user_oid != '0'::oid)
);

CREATE INDEX IF NOT EXISTS helio_index_queue_indexid_cmdtype on helio_api_catalog.helio_index_queue (index_id, cmd_type);
CREATE INDEX IF NOT EXISTS helio_index_queue_cmdtype_collectionid_cmdstatus on helio_api_catalog.helio_index_queue (cmd_type, collection_id, index_cmd_status);

GRANT SELECT ON TABLE helio_api_catalog.helio_index_queue TO public;
GRANT ALL ON TABLE helio_api_catalog.helio_index_queue TO helio_admin_role;