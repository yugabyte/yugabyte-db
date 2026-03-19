CREATE SCHEMA if not exists dist_rag;

CREATE TYPE dist_rag.secrets_provider_enum AS ENUM (
  'LOCAL', 'AWS', 'GCP', 'AZURE', 'HASHICORP_VAULT'
);

CREATE TYPE dist_rag.create_source_status_enum AS ENUM (
  'QUEUED', 'IN_PROGRESS', 'COMPLETED', 'FAILED'
);

CREATE TABLE dist_rag.sources (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  source_uri TEXT,

  --metadata filters for a specific source
  metadata JSONB,
  created_at TIMESTAMP NOT NULL DEFAULT NOW(),
  completed_at TIMESTAMP,
  status dist_rag.create_source_status_enum NOT NULL DEFAULT 'QUEUED',
  -- security
  secrets_provider dist_rag.secrets_provider_enum NOT NULL DEFAULT 'LOCAL',
  secrets_provider_params JSONB
 
);

CREATE TYPE dist_rag.ai_provider_enum AS ENUM (
  'OPENAI', 'LOCAL'
);

CREATE TYPE dist_rag.index_build_status AS ENUM (
  'INIT', 'IN_PROGRESS', 'NOT_STARTED'
);

CREATE TABLE dist_rag.vector_indexes (
  
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),

  -- Vector Index details
  index_name VARCHAR(50) NOT NULL DEFAULT 'pg_rag_default_store',
  -- Specify m and ef construction 
  index_options JSONB,
  index_creation_status dist_rag.index_build_status NOT NULL,

  -- model provider and API details
  ai_provider dist_rag.ai_provider_enum NOT NULL,
  embedding_model_params JSONB NOT NULL DEFAULT '{}',
  secrets_provider dist_rag.secrets_provider_enum NOT NULL DEFAULT 'LOCAL',
  secrets_provider_params JSONB

);

CREATE UNIQUE INDEX idx_vector_indexes_index_name ON dist_rag.vector_indexes(index_name);



CREATE TABLE dist_rag.vector_index_source_mappings (

  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  index_id UUID NOT NULL REFERENCES dist_rag.vector_indexes(id),
  source_id UUID NOT NULL REFERENCES dist_rag.sources(id),

  --chunk    
  chunk_params JSONB NOT NULL DEFAULT '{}'

);

CREATE INDEX idx_vector_index_source_mappings_index_id_source_id ON dist_rag.vector_index_source_mappings(index_id, source_id);


CREATE TYPE dist_rag.document_processing_status_enum AS ENUM (
  'NOT_STARTED', 'QUEUED', 'PROCESSING', 'COMPLETED', 'FAILED', 'RETRY'
);

CREATE TABLE dist_rag.documents (
  document_id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  source_id UUID NOT NULL REFERENCES dist_rag.sources(id),

  -- indiviual document metadata
  document_name TEXT,
  document_uri TEXT,
  document_checksum TEXT,

  -- Current state
  status dist_rag.document_processing_status_enum NOT NULL DEFAULT 'QUEUED'

);

CREATE INDEX idx_documents_source_id ON dist_rag.documents(source_id);

CREATE TYPE dist_rag.pipeline_status_enum AS ENUM (
  'PROCESSING', 'COMPLETED', 'FAILED'
);

CREATE TABLE dist_rag.pipeline_details (
  pipeline_id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  document_id UUID NOT NULL REFERENCES dist_rag.documents(document_id),
  document_name TEXT NOT NULL,
  
  -- Current state
  status dist_rag.pipeline_status_enum NOT NULL DEFAULT 'PROCESSING',
  
  -- Timing
  created_at TIMESTAMP NOT NULL DEFAULT NOW(),
  last_started_at TIMESTAMP,
  completed_at TIMESTAMP,

  -- embedding generation tracking
  chunks_processed INTEGER,
  embeddings_persisted INTEGER,
  
  -- Metadata (captured at pipeline start, never changes)
  metadata_snapshot JSONB NOT NULL,
  
  -- Progress tracking
  current_step VARCHAR(50),
  last_error_message TEXT
);

CREATE INDEX idx_pipeline_details_document_id ON dist_rag.pipeline_details(document_id);

CREATE TYPE dist_rag.task_type_enum AS ENUM(
	'CREATE_SOURCE','PREPROCESS'
);

CREATE TYPE dist_rag.task_queue_status_enum AS ENUM (
  'QUEUED', 'IN_PROGRESS', 'COMPLETED', 'FAILED'
);

CREATE TABLE dist_rag.work_queue (

  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  -- task details
  task_type dist_rag.task_type_enum NOT NULL,
  task_status dist_rag.task_queue_status_enum NOT NULL DEFAULT 'QUEUED',
  task_details JSONB NOT NULL,
  current_worker UUID,

  -- lease/optimistic locking
  lease_token UUID UNIQUE,                    
  lease_acquired_at TIMESTAMP,                
  lease_expires_at TIMESTAMP,

  -- process timestamps
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  started_at TIMESTAMP, 
  completed_at TIMESTAMP

);

-- Index optimized for worker task polling: filters by task_status and orders by created_at
CREATE INDEX idx_work_queue_task_status_created_at ON dist_rag.work_queue(task_status, created_at);

CREATE OR REPLACE FUNCTION dist_rag.create_source(
    r_source_uri TEXT,
    r_metadata JSONB DEFAULT '{}',
    r_secrets_provider dist_rag.secrets_provider_enum DEFAULT 'LOCAL',
    r_secrets_provider_params JSONB DEFAULT '{}'
)
RETURNS UUID
LANGUAGE plpgsql
SECURITY DEFINER
AS $$
DECLARE
    v_id UUID;
BEGIN
    -- Validate required parameter
    IF r_source_uri IS NULL OR r_source_uri = '' THEN
        RAISE EXCEPTION 'source_uri is required and cannot be NULL or empty';
    END IF;

    -- Insert source and capture the ID
    INSERT INTO dist_rag.sources (source_uri, metadata, secrets_provider, secrets_provider_params)
    VALUES (r_source_uri, COALESCE(r_metadata, '{}'::jsonb), r_secrets_provider, r_secrets_provider_params)
    RETURNING id INTO v_id;

    -- Validate insertion
    IF v_id IS NULL THEN
        RAISE EXCEPTION 'Failed to create source - no ID returned';
    END IF;

    -- Insert work queue entry for source creation
    INSERT INTO dist_rag.work_queue (task_type, task_status, task_details)
    VALUES (
        'CREATE_SOURCE'::dist_rag.task_type_enum, 
        'QUEUED'::dist_rag.task_queue_status_enum,
        jsonb_build_object('source_id', v_id)
    );

    RETURN v_id;
EXCEPTION WHEN OTHERS THEN
    RAISE EXCEPTION 'Error creating source: % - %', SQLSTATE, SQLERRM;
END;
$$;

COMMENT ON FUNCTION dist_rag.create_source(TEXT, JSONB, dist_rag.secrets_provider_enum, JSONB)
IS 'Create a new RAG source';


CREATE OR REPLACE FUNCTION dist_rag._create_vector_index_table(
    r_index_name VARCHAR(50),
    r_vector_dimensions INTEGER,
    r_index_options JSONB DEFAULT '{}'
)
RETURNS VOID
LANGUAGE plpgsql
SECURITY DEFINER
AS $$
DECLARE
    v_distance_metric TEXT;
    v_m INTEGER;
    v_ef_construction INTEGER;
    v_ops_class TEXT;
BEGIN

    -- validate required parameters
    IF r_index_name IS NULL OR r_index_name = '' THEN
        RAISE EXCEPTION 'index_name is required and cannot be NULL or empty';
    END IF;

    IF r_vector_dimensions IS NULL OR r_vector_dimensions <= 0 THEN
        RAISE EXCEPTION 'vector_dimensions is required and cannot be NULL or less than 1';
    END IF;

    -- Extract index options with defaults
    v_distance_metric := COALESCE(r_index_options ->> 'distance_metric', 'cosine');
    v_m := COALESCE((r_index_options ->> 'm')::INTEGER, 16);
    v_ef_construction := COALESCE((r_index_options ->> 'ef_construction')::INTEGER, 64);

    -- Map friendly distance metric name to pgvector operator class
    v_ops_class := CASE v_distance_metric
        WHEN 'cosine' THEN 'vector_cosine_ops'
        WHEN 'l2' THEN 'vector_l2_ops'
        WHEN 'ip' THEN 'vector_ip_ops'
        ELSE NULL
    END;

    IF v_ops_class IS NULL THEN
        RAISE EXCEPTION 'Invalid distance_metric "%". Must be one of: cosine, l2, ip', v_distance_metric;
    END IF;

    -- Create the vector store table
    EXECUTE 'CREATE TABLE ' || quote_ident(r_index_name) || ' (
        id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
        chunk_text TEXT NOT NULL,
        embeddings vector(' || r_vector_dimensions || ') NOT NULL,
        document_id UUID NOT NULL,
        metadata_filters JSONB NOT NULL DEFAULT ''' || '{}' || '''
    )';

    -- Create HNSW index on the embeddings column
    EXECUTE 'CREATE INDEX ' || quote_ident('idx_' || r_index_name || '_embeddings')
        || ' ON ' || quote_ident(r_index_name)
        || ' USING ybhnsw (embeddings ' || v_ops_class || ')'
        || ' WITH (m = ' || v_m || ', ef_construction = ' || v_ef_construction || ')';

END;
$$;

CREATE OR REPLACE FUNCTION dist_rag.init_vector_index(
    r_index_name VARCHAR(50) DEFAULT 'pg_rag_default_store',
    r_sources UUID[] DEFAULT ARRAY[]::UUID[],
    r_chunk_params JSONB DEFAULT '{}',
    r_ai_provider dist_rag.ai_provider_enum DEFAULT 'OPENAI',
    r_embedding_model_params JSONB DEFAULT '{}',
    r_index_options JSONB DEFAULT '{"distance_metric": "cosine", "m": 16, "ef_construction": 64}'
)
RETURNS UUID
LANGUAGE plpgsql
SECURITY DEFINER
AS $$
DECLARE
    v_index_id UUID;
    v_source UUID;
    v_vector_dimensions INTEGER;
    v_source_count INTEGER;
    v_sources_processed INTEGER := 0;
BEGIN

    -- Validate required parameters
    IF r_index_name IS NULL OR r_index_name = '' THEN
        RAISE EXCEPTION 'index_name is required and cannot be NULL or empty';
    END IF;

    -- Check if the index_name already exists and raise an error if it does
    IF EXISTS (
        SELECT 1 FROM dist_rag.vector_indexes WHERE index_name = r_index_name
    ) THEN
        RAISE EXCEPTION 'Vector index with name "%" already exists', r_index_name;
    END IF;

    -- Validate embedding model parameters contain dimensions
    IF r_embedding_model_params IS NULL OR r_embedding_model_params ->> 'dimensions' IS NULL THEN
        RAISE EXCEPTION 'embedding_model_params must contain "dimensions" key';
    END IF;

    -- Extract and validate vector dimensions
    v_vector_dimensions := (r_embedding_model_params ->> 'dimensions')::INTEGER;
    IF v_vector_dimensions IS NULL OR v_vector_dimensions <= 0 THEN
        RAISE EXCEPTION 'vector_dimensions must be a positive integer';
    END IF;

    -- Validate all source IDs exist before proceeding (fail-fast check)
    IF r_sources IS NOT NULL AND array_length(r_sources, 1) > 0 THEN
        SELECT COUNT(*) INTO v_source_count
        FROM dist_rag.sources
        WHERE id = ANY(r_sources);
        
        IF v_source_count != array_length(r_sources, 1) THEN
            RAISE EXCEPTION 'One or more source IDs do not exist in dist_rag.sources';
        END IF;
    END IF;

    -- BEGIN TRANSACTION: All operations below must succeed together or rollback together
    
    -- Step 1: Create the vector index table and HNSW index on embeddings
    BEGIN
        PERFORM dist_rag._create_vector_index_table(r_index_name, v_vector_dimensions, r_index_options);
    EXCEPTION WHEN OTHERS THEN
        RAISE EXCEPTION 'Failed to create vector index table "%": % - %', r_index_name, SQLSTATE, SQLERRM;
    END;

    -- Step 2: Insert vector index metadata
    BEGIN
        INSERT INTO dist_rag.vector_indexes (
            index_name, 
            index_options,
            index_creation_status, 
            ai_provider, 
            embedding_model_params
        )
        VALUES (
            r_index_name, 
            r_index_options,
            'INIT'::dist_rag.index_build_status, 
            r_ai_provider, 
            r_embedding_model_params
        )
        RETURNING id INTO v_index_id;
        
        IF v_index_id IS NULL THEN
            RAISE EXCEPTION 'Failed to insert vector index metadata - no ID returned';
        END IF;
    EXCEPTION WHEN OTHERS THEN
        RAISE EXCEPTION 'Failed to insert vector index metadata: % - %', SQLSTATE, SQLERRM;
    END;

    -- Step 3: Create mappings for each source (this will validate foreign keys)
    IF r_sources IS NOT NULL AND array_length(r_sources, 1) > 0 THEN
        BEGIN
            FOREACH v_source IN ARRAY r_sources LOOP
                INSERT INTO dist_rag.vector_index_source_mappings (index_id, source_id, chunk_params)
                VALUES (v_index_id, v_source, r_chunk_params);
                v_sources_processed := v_sources_processed + 1;
            END LOOP;
        EXCEPTION WHEN OTHERS THEN
            RAISE EXCEPTION 'Failed to create source mappings (processed % of % sources): % - %', 
                v_sources_processed, array_length(r_sources, 1), SQLSTATE, SQLERRM;
        END;
    END IF;

    RETURN v_index_id;
    
EXCEPTION WHEN OTHERS THEN
    -- If any step fails, the entire transaction will be rolled back
    RAISE EXCEPTION 'Error initializing vector index: % - %', SQLSTATE, SQLERRM;
END;
$$;

COMMENT ON FUNCTION dist_rag.init_vector_index(VARCHAR, UUID[], JSONB, dist_rag.ai_provider_enum, JSONB, JSONB)
IS 'Initialize a new vector index with optional HNSW index configuration via r_index_options';

CREATE OR REPLACE FUNCTION dist_rag.add_source_to_index(
    r_index_id UUID,
    r_source_id UUID,
    r_chunk_params JSONB DEFAULT '{}'
)
RETURNS VOID
LANGUAGE plpgsql
SECURITY DEFINER
AS $$
BEGIN
    INSERT INTO dist_rag.vector_index_source_mappings (index_id, source_id, chunk_params)
    VALUES (r_index_id, r_source_id, r_chunk_params);
EXCEPTION WHEN OTHERS THEN
    RAISE EXCEPTION 'Error adding source to index: % - %', SQLSTATE, SQLERRM;
END;
$$;

COMMENT ON FUNCTION dist_rag.add_source_to_index(UUID, UUID, JSONB)
IS 'Add a source to a vector index';


CREATE OR REPLACE FUNCTION dist_rag._queue_source_documents(
    r_source_id UUID,
    r_index_id UUID
)
RETURNS INTEGER
LANGUAGE plpgsql
SECURITY DEFINER
AS $$
DECLARE
    v_document RECORD;
    v_count INTEGER := 0;
BEGIN
    -- Validate required parameter
    IF r_source_id IS NULL THEN
        RAISE EXCEPTION 'source_id is required and cannot be NULL';
    END IF;

    -- Validate required parameter
    IF r_index_id IS NULL THEN
        RAISE EXCEPTION 'index_id is required and cannot be NULL';
    END IF;

    -- Insert work queue entries for all documents in this source
    INSERT INTO dist_rag.work_queue (
        task_type,
        task_status,
        task_details,
        created_at
    )
    SELECT
        'PREPROCESS'::dist_rag.task_type_enum,
        'QUEUED'::dist_rag.task_queue_status_enum,
        jsonb_build_object(
            'index_id', r_index_id,
            'source_id', r_source_id,
            'document_id', d.document_id,
            'document_name', d.document_name,
            'document_uri', d.document_uri,
            'document_status', d.status
        ),
        NOW()
    FROM dist_rag.documents d
    WHERE d.source_id = r_source_id;

    GET DIAGNOSTICS v_count = ROW_COUNT;

    RAISE NOTICE 'Number of Documents Queued % for Preprocessing', v_count;
    RETURN v_count;

EXCEPTION WHEN OTHERS THEN
    RAISE EXCEPTION 'Error queuing documents for source %: % - %', r_source_id, SQLSTATE, SQLERRM;
END;
$$;

CREATE OR REPLACE FUNCTION dist_rag.build_index(
    r_index_id UUID DEFAULT NULL,
    r_index_name VARCHAR(50) DEFAULT NULL
)
RETURNS VOID
LANGUAGE plpgsql
SECURITY DEFINER
AS $$
DECLARE
    v_index_id UUID;
    v_count INTEGER;
BEGIN
    -- Validate that exactly one parameter is provided
    IF (r_index_id IS NULL AND r_index_name IS NULL) THEN
        RAISE EXCEPTION 'Either index_id or index_name is required and cannot both be NULL';
    END IF;
    
    IF (r_index_id IS NOT NULL AND r_index_name IS NOT NULL) THEN
        RAISE EXCEPTION 'Provide only one of index_id or index_name, not both';
    END IF;

    -- If index_name is provided, resolve it to index_id
    IF r_index_name IS NOT NULL THEN
        SELECT id INTO v_index_id
        FROM dist_rag.vector_indexes
        WHERE index_name = r_index_name;
        
        IF v_index_id IS NULL THEN
            RAISE EXCEPTION 'Vector index with name "%" does not exist', r_index_name;
        END IF;
    ELSE
        v_index_id := r_index_id;
    END IF;

    -- Queue all documents for all sources associated with this index
    INSERT INTO dist_rag.work_queue (
        task_type,
        task_status,
        task_details,
        created_at
    )
    SELECT
        'PREPROCESS'::dist_rag.task_type_enum,
        'QUEUED'::dist_rag.task_queue_status_enum,
        jsonb_build_object(
            'index_id', v_index_id,
            'source_id', m.source_id,
            'document_id', d.document_id,
            'document_name', d.document_name,
            'document_uri', d.document_uri,
            'document_status', d.status
        ),
        NOW()
    FROM dist_rag.vector_index_source_mappings m
    JOIN dist_rag.documents d ON d.source_id = m.source_id
    WHERE m.index_id = v_index_id;

    GET DIAGNOSTICS v_count = ROW_COUNT;

    RAISE NOTICE 'Index build kicked off successfully for index_id: %, documents queued: %', v_index_id, v_count;

EXCEPTION WHEN OTHERS THEN
    RAISE EXCEPTION 'Error building index: % - %', SQLSTATE, SQLERRM;
END;
$$;

-- ============================================
-- VIEWS
-- ============================================


CREATE OR REPLACE VIEW dist_rag.vector_index_pipeline_details AS
SELECT 
    vi.id as index_id,
    vi.index_name,
    vi.ai_provider,
    vi.index_creation_status,
    s.id as source_id,
    s.source_uri,
    d.document_id,
    d.document_name,
    d.document_uri,
    d.document_checksum,
    d.status as document_status,
    pd.pipeline_id,
    pd.status as pipeline_status,
    pd.chunks_processed,
    pd.embeddings_persisted,
    pd.current_step,
    pd.last_error_message,
    pd.created_at as pipeline_created_at,
    pd.last_started_at as pipeline_last_started_at,
    pd.completed_at as pipeline_completed_at,
    pd.metadata_snapshot
FROM dist_rag.vector_indexes vi
INNER JOIN dist_rag.vector_index_source_mappings vism ON vi.id = vism.index_id
INNER JOIN dist_rag.sources s ON vism.source_id = s.id
INNER JOIN dist_rag.documents d ON s.id = d.source_id
LEFT JOIN dist_rag.pipeline_details pd ON d.document_id = pd.document_id
ORDER BY vi.index_name, s.source_uri, d.document_name, pd.created_at DESC;

COMMENT ON VIEW dist_rag.vector_index_pipeline_details
IS 'Shows all pipeline details for documents across all vector indexes. Query by index_id or index_name to filter results.';

CREATE OR REPLACE VIEW dist_rag.pipeline_stats AS
SELECT 
    vi.id as index_id,
    vi.index_name,
    vi.ai_provider,
    s.source_uri,
    d.document_id,
    d.document_name,
    COUNT(pd.pipeline_id) as calls,
    COALESCE(SUM(pd.chunks_processed), 0) as total_chunks_processed,
    COALESCE(SUM(pd.embeddings_persisted), 0) as total_embeddings_persisted,
    COALESCE(SUM(EXTRACT(EPOCH FROM (pd.completed_at - pd.created_at))), 0) as total_exec_time_seconds,
    COUNT(CASE WHEN pd.status = 'COMPLETED' THEN 1 END) as successful_completions,
    ROUND(100.0 * COUNT(CASE WHEN pd.status = 'COMPLETED' THEN 1 END) / NULLIF(COUNT(pd.pipeline_id), 0), 2) as completion_rate_percent,
    MAX(pd.completed_at) as last_completed_at,
    (array_agg(pd.last_error_message ORDER BY pd.created_at DESC) FILTER (WHERE pd.last_error_message IS NOT NULL))[1] as last_error_message,
    MIN(pd.created_at) as first_pipeline_started_at
FROM dist_rag.vector_indexes vi
INNER JOIN dist_rag.vector_index_source_mappings vism ON vi.id = vism.index_id
INNER JOIN dist_rag.sources s ON vism.source_id = s.id
INNER JOIN dist_rag.documents d ON s.id = d.source_id
LEFT JOIN dist_rag.pipeline_details pd ON d.document_id = pd.document_id
GROUP BY vi.id, vi.index_name, vi.ai_provider, s.source_uri, d.document_id, d.document_name
ORDER BY vi.index_name, s.source_uri, d.document_name;

COMMENT ON VIEW dist_rag.pipeline_stats
IS 'Shows all pipeline stats for all vector indexes. Query by index_id or index_name to filter results.';

-- ============================================
-- PERMISSIONS
-- ============================================
GRANT USAGE ON SCHEMA dist_rag TO PUBLIC;
GRANT ALL ON ALL SEQUENCES IN SCHEMA dist_rag TO PUBLIC;
GRANT ALL ON ALL TABLES IN SCHEMA dist_rag TO PUBLIC;
GRANT ALL ON ALL FUNCTIONS IN SCHEMA dist_rag TO PUBLIC;
