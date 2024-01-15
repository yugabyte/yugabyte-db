ALTER TABLE __API_CATALOG_SCHEMA__.__EXTENSION_OBJECT__(_index_queue) DROP CONSTRAINT IF EXISTS __EXTENSION_OBJECT__(_index_queue_cmd_type_check);
ALTER TABLE __API_CATALOG_SCHEMA__.__EXTENSION_OBJECT__(_index_queue) ADD CONSTRAINT __EXTENSION_OBJECT__(_index_queue_cmd_type_check) CHECK (cmd_type IN ('C', 'R'));
ALTER TABLE __API_CATALOG_SCHEMA__.__EXTENSION_OBJECT__(_index_queue) ADD COLUMN IF NOT EXISTS global_pid bigint, 
                                                                        ADD COLUMN IF NOT EXISTS start_time timestamp WITH TIME ZONE, 
                                                                        DROP COLUMN IF EXISTS op_id,
                                                                        ADD COLUMN IF NOT EXISTS collection_id bigint not null;
