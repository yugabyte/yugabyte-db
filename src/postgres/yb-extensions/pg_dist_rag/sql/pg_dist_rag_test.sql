-- ============================================
-- pg_dist_rag Extension - Test Suite
-- ============================================
-- This file can be used to validate the extension functionality
-- Run with: psql -U postgres -d testdb -f sql/pg_dist_rag_test.sql

-- Create extension (if not already created)
CREATE EXTENSION IF NOT EXISTS pg_dist_rag;

-- ============================================
-- Test 1: Create a basic source
-- ============================================
DO $$
DECLARE
  v_source_id UUID;
BEGIN
  RAISE NOTICE '=== Test 1: Create a basic source ===';
  v_source_id := dist_rag.create_source(
    r_source_uri := 's3://proto-automated-embedding/playgentic_documents/sample/'
  );
  ASSERT v_source_id IS NOT NULL, 'Source ID should not be NULL';
  RAISE NOTICE 'PASS: Created source with ID %', v_source_id;
END $$;

-- ================================================
-- Test 2: Create source with metadata and secrets
-- ================================================
DO $$
DECLARE
  v_source_id UUID;
  v_metadata JSONB;
  v_secrets JSONB;
BEGIN
  RAISE NOTICE '=== Test 2: Create source with metadata and secrets ===';
  v_metadata := jsonb_build_object('language', 'english', 'type', 'documentation');
  v_secrets := jsonb_build_object('api_key', 'secret123', 'region', 'us-east-1');
  v_source_id := dist_rag.create_source(
    r_source_uri := 's3://proto-automated-embedding/playgentic_documents/sample/',
    r_metadata := v_metadata,
    r_secrets_provider := 'AWS',
    r_secrets_provider_params := v_secrets
  );
  ASSERT v_source_id IS NOT NULL, 'Source ID should not be NULL';
  -- Verify metadata was stored
  ASSERT (SELECT metadata->>'language' FROM dist_rag.sources WHERE id = v_source_id) = 'english',
    'Metadata should be stored correctly';
  RAISE NOTICE 'PASS: Created source with metadata - ID: %', v_source_id;
END $$;

-- ============================================
-- Test 3: Create source and verify work queue entry
-- ============================================
DO $$
DECLARE
  v_source_id UUID;
  v_work_queue_count INT;
BEGIN
  RAISE NOTICE '=== Test 3: Create source and verify work queue entry ===';
  v_source_id := dist_rag.create_source(
    r_source_uri := 's3://proto-automated-embedding/playgentic_documents/sample/'
  );
  -- Verify work queue entry was created
  SELECT COUNT(*) INTO v_work_queue_count
  FROM dist_rag.work_queue
  WHERE task_details->>'source_id' = v_source_id::TEXT
    AND task_type = 'CREATE_SOURCE'::dist_rag.task_type_enum;
  ASSERT v_work_queue_count = 1, 'Should have exactly one work queue entry';
  RAISE NOTICE 'PASS: Work queue entry created for source %', v_source_id;
END $$;

-- ============================================
-- Test 4: Initialize vector index with sources
-- ============================================
DO $$
DECLARE
  v_source_id_1 UUID;
  v_source_id_2 UUID;
  v_index_id UUID;
  v_mapping_count INT;
BEGIN
  RAISE NOTICE '=== Test 4: Initialize vector index with sources ===';
  -- Create two sources
  v_source_id_1 := dist_rag.create_source(r_source_uri := 's3://proto-automated-embedding/playgentic_documents/sample/');
  v_source_id_2 := dist_rag.create_source(r_source_uri := 's3://proto-automated-embedding/playgentic_documents/drills/');
  -- Initialize vector index with both sources
  v_index_id := dist_rag.init_vector_index(
    r_index_name := 'test_index_1',
    r_sources := ARRAY[v_source_id_1, v_source_id_2]::UUID[],
    r_ai_provider := 'OPENAI',
    r_embedding_model_params := jsonb_build_object('dimensions', 1536)
  );
  ASSERT v_index_id IS NOT NULL, 'Index ID should not be NULL';
  -- Verify mappings were created
  SELECT COUNT(*) INTO v_mapping_count
  FROM dist_rag.vector_index_source_mappings
  WHERE index_id = v_index_id;
  ASSERT v_mapping_count = 2, 'Should have 2 source mappings';
  RAISE NOTICE 'PASS: Vector index created with % source mappings', v_mapping_count;
END $$;

-- ============================================
-- Test 4b: Initialize vector index with defaults
-- ============================================
DO $$
DECLARE
  v_index_id UUID;
  v_mapping_count INT;
BEGIN
  RAISE NOTICE '=== Test 4b: Initialize vector index with defaults (no sources) ===';
  -- Initialize vector index with only index name (using default empty sources array)
  v_index_id := dist_rag.init_vector_index(
    r_index_name := 'test_index_defaults',
    r_embedding_model_params := jsonb_build_object('dimensions', 1536)
  );
  ASSERT v_index_id IS NOT NULL, 'Index ID should not be NULL';
  -- Verify no mappings were created (because sources is empty array)
  SELECT COUNT(*) INTO v_mapping_count
  FROM dist_rag.vector_index_source_mappings
  WHERE index_id = v_index_id;
  ASSERT v_mapping_count = 0, 'Should have 0 source mappings when no sources provided';
  RAISE NOTICE 'PASS: Vector index created with defaults - mappings: %', v_mapping_count;
END $$;

-- ============================================
-- Test 5: Add source to existing index
-- ============================================
DO $$
DECLARE
  v_source_id UUID;
  v_index_id UUID;
  v_mapping_count INT;
BEGIN
  RAISE NOTICE '=== Test 5: Add source to existing index ===';
  -- Create a source and index (with default empty sources)
  v_source_id := dist_rag.create_source(r_source_uri := 's3://proto-automated-embedding/playgentic_documents/drills/');
  v_index_id := dist_rag.init_vector_index(
    r_index_name := 'test_index_2',
    r_embedding_model_params := jsonb_build_object('dimensions', 1536)
    -- r_sources uses default empty array
  );
  -- Add first source to the index
  PERFORM dist_rag.add_source_to_index(
    r_index_id := v_index_id,
    r_source_id := v_source_id
  );
  -- Add second source to the index
  PERFORM dist_rag.add_source_to_index(
    r_index_id := v_index_id,
    r_source_id := (SELECT id FROM dist_rag.sources LIMIT 1 OFFSET 1)
  );
  -- Verify the mappings were added
  SELECT COUNT(*) INTO v_mapping_count
  FROM dist_rag.vector_index_source_mappings
  WHERE index_id = v_index_id;
  ASSERT v_mapping_count = 2, 'Should have 2 source mappings after adding';
  RAISE NOTICE 'PASS: Sources added to index - total mappings: %', v_mapping_count;
END $$;

-- ============================================
-- Test 6: Queue source documents
-- ============================================
DO $$
DECLARE
  v_source_id UUID;
  v_doc_1 UUID;
  v_doc_2 UUID;
  v_index_id UUID;
  v_queued_count INT;
BEGIN
  RAISE NOTICE '=== Test 6: Queue source documents ===';
  -- Create a source
  v_source_id := dist_rag.create_source(r_source_uri := 's3://proto-automated-embedding/playgentic_documents/drills/');
  -- Create an index for this source
  v_index_id := dist_rag.init_vector_index(
    r_index_name := 'queue_test_index',
    r_sources := ARRAY[v_source_id]::UUID[],
    r_embedding_model_params := jsonb_build_object('dimensions', 1536)
  );
  -- Add documents for this source
  INSERT INTO dist_rag.documents (source_id, document_name, document_uri, status)
  VALUES (v_source_id, 'doc1.pdf', 'https://example.com/doc1.pdf', 'QUEUED'::dist_rag.document_processing_status_enum)
  RETURNING document_id INTO v_doc_1;
  INSERT INTO dist_rag.documents (source_id, document_name, document_uri, status)
  VALUES (v_source_id, 'doc2.txt', 'https://example.com/doc2.txt', 'QUEUED'::dist_rag.document_processing_status_enum)
  RETURNING document_id INTO v_doc_2;
  -- Queue documents
  v_queued_count := dist_rag._queue_source_documents(v_source_id, v_index_id);
  ASSERT v_queued_count = 2, 'Should have queued 2 documents';
  -- Verify work queue entries
  SELECT COUNT(*) INTO v_queued_count
  FROM dist_rag.work_queue
  WHERE task_type = 'PREPROCESS'::dist_rag.task_type_enum
    AND task_details->>'source_id' = v_source_id::TEXT
    AND task_details->>'index_id' = v_index_id::TEXT;
  ASSERT v_queued_count = 2, 'Should have 2 preprocess tasks in work queue';
  RAISE NOTICE 'PASS: Queued % documents from source', v_queued_count;
END $$;

-- ============================================
-- Test 7: Build index (full workflow)
-- ============================================
DO $$
DECLARE
  v_source_id_1 UUID;
  v_source_id_2 UUID;
  v_index_id UUID;
  v_preprocess_count INT;
  v_doc_count INT;
BEGIN
  RAISE NOTICE '=== Test 7: Build index (full workflow) ===';
  -- Create sources
  v_source_id_1 := dist_rag.create_source(r_source_uri := 'https://source1.com/docs/');
  v_source_id_2 := dist_rag.create_source(r_source_uri := 'https://source2.com/docs/');
  -- Add documents to sources
  INSERT INTO dist_rag.documents (source_id, document_name, document_uri)
  VALUES (v_source_id_1, 'report1.pdf', 'https://example.com/report1.pdf'),
    (v_source_id_1, 'report2.pdf', 'https://example.com/report2.pdf'),
    (v_source_id_2, 'guide1.pdf', 'https://example.com/guide1.pdf');
  -- Get document count
  SELECT COUNT(*) INTO v_doc_count FROM dist_rag.documents WHERE source_id IN (v_source_id_1, v_source_id_2);
  -- Initialize index with both sources
  v_index_id := dist_rag.init_vector_index(
    r_index_name := 'build_test_index',
    r_sources := ARRAY[v_source_id_1, v_source_id_2]::UUID[],
    r_embedding_model_params := jsonb_build_object('dimensions', 1536)
  );
  -- Build the index
  PERFORM dist_rag.build_index(v_index_id);
  -- Verify preprocess tasks were queued
  SELECT COUNT(*) INTO v_preprocess_count
  FROM dist_rag.work_queue
  WHERE task_type = 'PREPROCESS'::dist_rag.task_type_enum
    AND task_details->>'index_id' = v_index_id::TEXT;
  ASSERT v_preprocess_count >= v_doc_count, 'Should have queued all documents';
  RAISE NOTICE 'PASS: Index built with % documents queued for preprocessing', v_preprocess_count;
END $$;

-- ============================================
-- Test 7a: Build index is idempotent w.r.t. PROCESSING/COMPLETED documents
-- ============================================
-- Regression: build_index used to re-queue every document on every call,
-- which inflated the work queue and (when workers picked the duplicates
-- up) caused N x duplicate embeddings in datapacks.knowledge_bases.
-- The function now filters out documents whose status is PROCESSING or
-- COMPLETED so re-running it on a built index is a no-op.
DO $$
DECLARE
  v_source_id UUID;
  v_index_id UUID;
  v_doc_done UUID;
  v_doc_inflight UUID;
  v_doc_failed UUID;
  v_doc_queued UUID;
  v_first_count INT;
  v_second_count INT;
BEGIN
  RAISE NOTICE '=== Test 7a: build_index skips PROCESSING/COMPLETED documents ===';
  v_source_id := dist_rag.create_source(r_source_uri := 'https://idempotent.example.com/docs/');
  v_index_id := dist_rag.init_vector_index(
    r_index_name := 'idempotent_build_test',
    r_sources := ARRAY[v_source_id]::UUID[],
    r_embedding_model_params := jsonb_build_object('dimensions', 1536)
  );

  INSERT INTO dist_rag.documents (source_id, document_name, document_uri, status)
  VALUES (v_source_id, 'completed.pdf', 'https://idempotent.example.com/completed.pdf',
          'COMPLETED'::dist_rag.document_processing_status_enum)
  RETURNING document_id INTO v_doc_done;

  INSERT INTO dist_rag.documents (source_id, document_name, document_uri, status)
  VALUES (v_source_id, 'processing.pdf', 'https://idempotent.example.com/processing.pdf',
          'PROCESSING'::dist_rag.document_processing_status_enum)
  RETURNING document_id INTO v_doc_inflight;

  INSERT INTO dist_rag.documents (source_id, document_name, document_uri, status)
  VALUES (v_source_id, 'failed.pdf', 'https://idempotent.example.com/failed.pdf',
          'FAILED'::dist_rag.document_processing_status_enum)
  RETURNING document_id INTO v_doc_failed;

  INSERT INTO dist_rag.documents (source_id, document_name, document_uri, status)
  VALUES (v_source_id, 'queued.pdf', 'https://idempotent.example.com/queued.pdf',
          'QUEUED'::dist_rag.document_processing_status_enum)
  RETURNING document_id INTO v_doc_queued;

  -- First build: only QUEUED + FAILED should be enqueued.
  PERFORM dist_rag.build_index(v_index_id);
  SELECT COUNT(*) INTO v_first_count
  FROM dist_rag.work_queue
  WHERE task_type = 'PREPROCESS'::dist_rag.task_type_enum
    AND task_details->>'index_id' = v_index_id::TEXT;
  ASSERT v_first_count = 2,
    format('First build_index should enqueue 2 documents (QUEUED + FAILED), got %s', v_first_count);

  ASSERT EXISTS (
    SELECT 1 FROM dist_rag.work_queue
    WHERE task_details->>'document_id' = v_doc_queued::TEXT
  ), 'Queued document must be enqueued';
  ASSERT EXISTS (
    SELECT 1 FROM dist_rag.work_queue
    WHERE task_details->>'document_id' = v_doc_failed::TEXT
  ), 'Failed document must be enqueued (acts as retry path)';
  ASSERT NOT EXISTS (
    SELECT 1 FROM dist_rag.work_queue
    WHERE task_details->>'document_id' = v_doc_done::TEXT
  ), 'Completed document must NOT be enqueued';
  ASSERT NOT EXISTS (
    SELECT 1 FROM dist_rag.work_queue
    WHERE task_details->>'document_id' = v_doc_inflight::TEXT
  ), 'Processing document must NOT be enqueued';

  -- Second build: still no enqueue for COMPLETED/PROCESSING. The QUEUED
  -- and FAILED rows above are now duplicated -- that's a separate
  -- concern (NOT EXISTS guard against work_queue could be added later);
  -- the contract we're locking in here is that PROCESSING/COMPLETED
  -- documents are never re-enqueued.
  PERFORM dist_rag.build_index(v_index_id);
  SELECT COUNT(*) INTO v_second_count
  FROM dist_rag.work_queue
  WHERE task_type = 'PREPROCESS'::dist_rag.task_type_enum
    AND task_details->>'index_id' = v_index_id::TEXT
    AND task_details->>'document_id' IN (v_doc_done::TEXT, v_doc_inflight::TEXT);
  ASSERT v_second_count = 0,
    format('Re-running build_index must not enqueue PROCESSING/COMPLETED docs, found %s', v_second_count);

  RAISE NOTICE 'PASS: build_index skips PROCESSING/COMPLETED docs and re-runs idempotently for them';
END $$;

-- ============================================
-- Test 7b: Build index by name
-- ============================================
DO $$
DECLARE
  v_source_id UUID;
  v_index_id UUID;
  v_preprocess_count INT;
BEGIN
  RAISE NOTICE '=== Test 7b: Build index by name ===';
  -- Create source and documents
  v_source_id := dist_rag.create_source(r_source_uri := 'https://byname.com/docs/');
  INSERT INTO dist_rag.documents (source_id, document_name, document_uri)
  VALUES (v_source_id, 'byname_doc.pdf', 'https://byname.com/byname_doc.pdf');
  -- Initialize index
  v_index_id := dist_rag.init_vector_index(
    r_index_name := 'build_by_name_index',
    r_sources := ARRAY[v_source_id]::UUID[],
    r_embedding_model_params := jsonb_build_object('dimensions', 1536)
  );
  -- Build index using name instead of ID
  PERFORM dist_rag.build_index(r_index_name := 'build_by_name_index');
  -- Verify preprocess tasks were queued
  SELECT COUNT(*) INTO v_preprocess_count
  FROM dist_rag.work_queue
  WHERE task_type = 'PREPROCESS'::dist_rag.task_type_enum
    AND task_details->>'index_id' = v_index_id::TEXT;
  ASSERT v_preprocess_count >= 1, 'Should have queued at least 1 document by name';
  RAISE NOTICE 'PASS: Build index by name queued % documents', v_preprocess_count;
END $$;

-- ============================================
-- Test 8: Error handling - NULL source_uri
-- ============================================
DO $$
BEGIN
  RAISE NOTICE '=== Test 9: Error handling - NULL source_uri ===';
  -- Try to create source without URI
  PERFORM dist_rag.create_source(r_source_uri := NULL);
  RAISE NOTICE 'FAIL: Should have raised an exception for NULL source_uri';
EXCEPTION WHEN OTHERS THEN
  RAISE NOTICE 'PASS: Correctly caught error when source_uri is NULL - %', SQLERRM;
END $$;

-- ============================================
-- Test 8b: Error handling - Empty string source_uri
-- ============================================
DO $$
BEGIN
  RAISE NOTICE '=== Test 9b: Error handling - Empty string source_uri ===';
  PERFORM dist_rag.create_source(r_source_uri := '');
  RAISE NOTICE 'FAIL: Should have raised an exception for empty source_uri';
EXCEPTION WHEN OTHERS THEN
  RAISE NOTICE 'PASS: Correctly caught error when source_uri is empty - %', SQLERRM;
END $$;

-- ============================================
-- Test 9: Error handling - Build index with no sources
-- ============================================
DO $$
DECLARE
  v_index_id UUID;
BEGIN
  RAISE NOTICE '=== Test 10: Build index with no sources (should handle gracefully) ===';
  -- Create an empty index
  v_index_id := dist_rag.init_vector_index(
    r_index_name := 'empty_index',
    r_embedding_model_params := jsonb_build_object('dimensions', 1536)
  );
  -- Build the empty index (should not error)
  PERFORM dist_rag.build_index(v_index_id);
  RAISE NOTICE 'PASS: Build index handled empty sources gracefully';
END $$;

-- ============================================
-- Test 10: Error handling - Duplicate index name
-- ============================================
DO $$
DECLARE
  v_index_id UUID;
BEGIN
  RAISE NOTICE '=== Test 11: Error handling - Duplicate index name ===';
  -- First index should succeed
  v_index_id := dist_rag.init_vector_index(
    r_index_name := 'duplicate_name_test',
    r_embedding_model_params := jsonb_build_object('dimensions', 1536)
  );
  -- Second index with same name should fail
  v_index_id := dist_rag.init_vector_index(
    r_index_name := 'duplicate_name_test',
    r_embedding_model_params := jsonb_build_object('dimensions', 1536)
  );
  RAISE NOTICE 'FAIL: Should have raised an exception for duplicate index name';
EXCEPTION WHEN OTHERS THEN
  RAISE NOTICE 'PASS: Correctly caught duplicate index name error - %', SQLERRM;
END $$;

-- ============================================
-- Test 11: Error handling - Missing dimensions in embedding_model_params
-- ============================================
DO $$
DECLARE
  v_index_id UUID;
BEGIN
  RAISE NOTICE '=== Test 12: Error handling - Missing dimensions in embedding_model_params ===';
  v_index_id := dist_rag.init_vector_index(
    r_index_name := 'missing_dims_index',
    r_embedding_model_params := jsonb_build_object('model', 'text-embedding-ada-002')
  );
  RAISE NOTICE 'FAIL: Should have raised an exception for missing dimensions';
EXCEPTION WHEN OTHERS THEN
  RAISE NOTICE 'PASS: Correctly caught missing dimensions error - %', SQLERRM;
END $$;

-- ============================================
-- Test 12: Error handling - Invalid source IDs in init_vector_index
-- ============================================
DO $$
DECLARE
  v_index_id UUID;
  v_fake_id UUID := gen_random_uuid();
BEGIN
  RAISE NOTICE '=== Test 13: Error handling - Invalid source IDs in init_vector_index ===';
  v_index_id := dist_rag.init_vector_index(
    r_index_name := 'invalid_sources_index',
    r_sources := ARRAY[v_fake_id]::UUID[],
    r_embedding_model_params := jsonb_build_object('dimensions', 1536)
  );
  RAISE NOTICE 'FAIL: Should have raised an exception for invalid source IDs';
EXCEPTION WHEN OTHERS THEN
  RAISE NOTICE 'PASS: Correctly caught invalid source IDs error - %', SQLERRM;
END $$;

-- ============================================
-- Test 13: Error handling - build_index with both params NULL
-- ============================================
DO $$
BEGIN
  RAISE NOTICE '=== Test 14: Error handling - build_index with both params NULL ===';
  PERFORM dist_rag.build_index(r_index_id := NULL, r_index_name := NULL);
  RAISE NOTICE 'FAIL: Should have raised an exception for both NULL params';
EXCEPTION WHEN OTHERS THEN
  RAISE NOTICE 'PASS: Correctly caught both NULL params error - %', SQLERRM;
END $$;

-- ============================================
-- Test 14: Error handling - build_index with both params provided
-- ============================================
DO $$
DECLARE
  v_source_id UUID;
  v_index_id UUID;
BEGIN
  RAISE NOTICE '=== Test 15: Error handling - build_index with both params provided ===';
  v_source_id := dist_rag.create_source(r_source_uri := 'https://both-params.com/docs/');
  v_index_id := dist_rag.init_vector_index(
    r_index_name := 'both_params_index',
    r_sources := ARRAY[v_source_id]::UUID[],
    r_embedding_model_params := jsonb_build_object('dimensions', 1536)
  );
  PERFORM dist_rag.build_index(r_index_id := v_index_id, r_index_name := 'both_params_index');
  RAISE NOTICE 'FAIL: Should have raised an exception for both params provided';
EXCEPTION WHEN OTHERS THEN
  RAISE NOTICE 'PASS: Correctly caught both params provided error - %', SQLERRM;
END $$;

-- ============================================
-- Test 15: Error handling - build_index with non-existent name
-- ============================================
DO $$
BEGIN
  RAISE NOTICE '=== Test 16: Error handling - build_index with non-existent name ===';
  PERFORM dist_rag.build_index(r_index_name := 'this_index_does_not_exist');
  RAISE NOTICE 'FAIL: Should have raised an exception for non-existent index name';
EXCEPTION WHEN OTHERS THEN
  RAISE NOTICE 'PASS: Correctly caught non-existent index name error - %', SQLERRM;
END $$;

-- ============================================
-- Test 16: add_source_to_index with custom chunk_params
-- ============================================
DO $$
DECLARE
  v_source_id UUID;
  v_index_id UUID;
  v_chunk_params JSONB;
  v_stored_params JSONB;
BEGIN
  RAISE NOTICE '=== Test 17: add_source_to_index with custom chunk_params ===';
  v_source_id := dist_rag.create_source(r_source_uri := 'https://chunk-params.com/docs/');
  v_index_id := dist_rag.init_vector_index(
    r_index_name := 'chunk_params_index',
    r_embedding_model_params := jsonb_build_object('dimensions', 1536)
  );
  v_chunk_params := jsonb_build_object('chunk_size', 512, 'overlap', 50, 'strategy', 'recursive');
  PERFORM dist_rag.add_source_to_index(
    r_index_id := v_index_id,
    r_source_id := v_source_id,
    r_chunk_params := v_chunk_params
  );
  -- Verify chunk_params were stored correctly
  SELECT chunk_params INTO v_stored_params
  FROM dist_rag.vector_index_source_mappings
  WHERE index_id = v_index_id AND source_id = v_source_id;
  ASSERT v_stored_params IS NOT NULL, 'chunk_params should be stored';
  ASSERT (v_stored_params->>'chunk_size')::INT = 512, 'chunk_size should be 512';
  ASSERT (v_stored_params->>'overlap')::INT = 50, 'overlap should be 50';
  RAISE NOTICE 'PASS: Custom chunk_params stored correctly - %', v_stored_params;
END $$;

-- ============================================
-- Test 16d: init_vector_index respects r_schema_name
-- ============================================
DO $$
DECLARE
  v_index_id UUID;
  v_table_schema TEXT;
  v_index_schema TEXT;
BEGIN
  RAISE NOTICE '=== Test 16d: init_vector_index creates backing table in r_schema_name ===';
  CREATE SCHEMA IF NOT EXISTS rag_custom_schema;
  v_index_id := dist_rag.init_vector_index(
    r_index_name := 'custom_schema_index',
    r_embedding_model_params := jsonb_build_object('dimensions', 1536),
    r_schema_name := 'rag_custom_schema'
  );
  ASSERT v_index_id IS NOT NULL, 'Index ID should not be NULL';

  -- Verify the backing table was created in the requested schema, not public.
  SELECT schemaname INTO v_table_schema
  FROM pg_tables
  WHERE tablename = 'custom_schema_index';
  ASSERT v_table_schema = 'rag_custom_schema',
    format('Expected backing table in rag_custom_schema, found in %s', v_table_schema);

  -- And the HNSW index should live alongside the table.
  SELECT schemaname INTO v_index_schema
  FROM pg_indexes
  WHERE indexname = 'idx_custom_schema_index_embeddings';
  ASSERT v_index_schema = 'rag_custom_schema',
    format('Expected HNSW index in rag_custom_schema, found in %s', v_index_schema);

  RAISE NOTICE 'PASS: backing table and index created in rag_custom_schema';
END $$;

-- ============================================
-- Test 16e: init_vector_index rejects missing schema
-- ============================================
DO $$
DECLARE
  v_index_id UUID;
BEGIN
  RAISE NOTICE '=== Test 16e: Reject init_vector_index with non-existent schema ===';
  v_index_id := dist_rag.init_vector_index(
    r_index_name := 'missing_schema_index',
    r_embedding_model_params := jsonb_build_object('dimensions', 1536),
    r_schema_name := 'this_schema_does_not_exist'
  );
  RAISE NOTICE 'FAIL: Should have raised an exception for missing schema';
EXCEPTION WHEN OTHERS THEN
  ASSERT SQLERRM LIKE '%does not exist%',
    format('Expected schema-missing error, got: %s', SQLERRM);
  RAISE NOTICE 'PASS: Correctly rejected missing schema - %', SQLERRM;
END $$;

-- ============================================
-- Test 17: Views - vector_index_pipeline_details
-- ============================================
DO $$
DECLARE
  v_source_id UUID;
  v_index_id UUID;
  v_doc_id UUID;
  v_view_count INT;
BEGIN
  RAISE NOTICE '=== Test 18: Views - vector_index_pipeline_details ===';
  -- Create source, index, and documents
  v_source_id := dist_rag.create_source(r_source_uri := 'https://view-test.com/docs/');
  v_index_id := dist_rag.init_vector_index(
    r_index_name := 'view_test_index',
    r_sources := ARRAY[v_source_id]::UUID[],
    r_embedding_model_params := jsonb_build_object('dimensions', 1536)
  );
  INSERT INTO dist_rag.documents (source_id, document_name, document_uri)
  VALUES (v_source_id, 'view_doc.pdf', 'https://view-test.com/view_doc.pdf')
  RETURNING document_id INTO v_doc_id;
  -- Insert a pipeline detail for this document
  INSERT INTO dist_rag.pipeline_details (
    document_id, document_name, status, chunks_processed, embeddings_persisted,
    current_step, metadata_snapshot, created_at, completed_at
  ) VALUES (
    v_doc_id, 'view_doc.pdf', 'COMPLETED', 10, 10,
    'DONE', '{"model": "text-embedding-ada-002"}'::jsonb, NOW() - interval '5 minutes', NOW()
  );
  -- Verify the view returns data
  SELECT COUNT(*) INTO v_view_count
  FROM dist_rag.vector_index_pipeline_details
  WHERE index_id = v_index_id;
  ASSERT v_view_count >= 1, 'vector_index_pipeline_details should have at least 1 row';
  RAISE NOTICE 'PASS: vector_index_pipeline_details returned % rows for index', v_view_count;
END $$;

-- ============================================
-- Test 19: Views - pipeline_stats
-- ============================================
DO $$
DECLARE
  v_stats_count INT;
  v_total_chunks INT;
  v_completion_rate NUMERIC;
BEGIN
  RAISE NOTICE '=== Test 19: Views - pipeline_stats ===';
  -- Use data created in Test 18
  SELECT COUNT(*) INTO v_stats_count
  FROM dist_rag.pipeline_stats
  WHERE index_name = 'view_test_index';
  ASSERT v_stats_count >= 1, 'pipeline_stats should have at least 1 row for view_test_index';
  -- Verify aggregated fields
  SELECT total_chunks_processed, completion_rate_percent
  INTO v_total_chunks, v_completion_rate
  FROM dist_rag.pipeline_stats
  WHERE index_name = 'view_test_index'
  LIMIT 1;
  ASSERT v_total_chunks >= 10, 'Should report at least 10 chunks processed';
  ASSERT v_completion_rate = 100.00, 'Completion rate should be 100% for 1 completed pipeline';
  RAISE NOTICE 'PASS: pipeline_stats - chunks: %, completion rate: % %%', v_total_chunks, v_completion_rate;
END $$;

-- ============================================
-- Test 20: create_column_embedding_mapping declares an uninitialized mapping
-- ============================================
DO $$
DECLARE
  v_mapping_id UUID;
  v_status dist_rag.column_embedding_registration_status_enum;
  v_vector_index_id UUID;
  v_tenant_col TEXT;
  v_priority_col TEXT;
  v_source_table TEXT;
  v_text_columns TEXT[];
  v_filter_by_columns JSONB;
  v_order_by JSONB;
  v_filters JSONB := jsonb_build_array(
    jsonb_build_object('source_filter_on_column', 'trace_id', 'target_value_from_column', 'conversation_id'),
    jsonb_build_object('source_filter_on_column', 'metadata', 'json_key', 'message_id', 'target_value_from_column', 'message_id'),
    jsonb_build_object('source_filter_on_column', 'project_id', 'resolve', jsonb_build_object(
      'schema', 'meko_system', 'table', 'langfuse_project_mapping',
      'select_column', 'langfuse_project_id', 'where_column', 'datapack_id',
      'where_value_from_row_column', 'tenant_id'
    ))
  );
BEGIN
  RAISE NOTICE '=== Test 20: create_column_embedding_mapping declares an uninitialized mapping ===';

  v_mapping_id := dist_rag.create_column_embedding_mapping(
    r_registration_name := 'conversation_history_search_langfuse_test',
    r_destination_schema := 'datapacks',
    r_destination_table := 'conversation_history_search',
    r_destination_embedding_column := 'embeddings',
    r_source_schema := 'clickhouse',
    r_source_table := 'observations',
    r_source_text_columns := ARRAY['input', 'output'],
    r_source_filter_by_columns := v_filters,
    r_destination_tenant_column := 'tenant_id',
    r_destination_priority_column := 'priority',
    r_source_order_by := jsonb_build_object('column', 'start_time', 'direction', 'ASC')
  );
  ASSERT v_mapping_id IS NOT NULL, 'Mapping ID should not be NULL';

  SELECT status, vector_index_id, destination_tenant_column, destination_priority_column,
         source_table, source_text_columns, source_filter_by_columns, source_order_by
  INTO v_status, v_vector_index_id, v_tenant_col, v_priority_col, v_source_table, v_text_columns,
       v_filter_by_columns, v_order_by
  FROM dist_rag.column_embedding_registrations
  WHERE id = v_mapping_id;

  ASSERT v_status = 'PAUSED'::dist_rag.column_embedding_registration_status_enum,
    'a freshly declared mapping should default to PAUSED (not yet initialized)';
  ASSERT v_vector_index_id IS NULL, 'vector_index_id should be NULL until init_column_embedding runs';
  ASSERT v_tenant_col = 'tenant_id', 'destination_tenant_column should round-trip';
  ASSERT v_priority_col = 'priority', 'destination_priority_column should round-trip';
  ASSERT v_source_table = 'observations', 'source_table should round-trip';
  ASSERT v_text_columns = ARRAY['input', 'output'], 'source_text_columns should round-trip';
  ASSERT jsonb_array_length(v_filter_by_columns) = 3, 'source_filter_by_columns should round-trip as a 3-element array';
  ASSERT v_filter_by_columns->0->>'target_value_from_column' = 'conversation_id',
    'row-derived filter value should round-trip';
  ASSERT v_filter_by_columns->2->'resolve'->>'where_value_from_row_column' = 'tenant_id',
    'resolver row-derived where-value should round-trip';
  ASSERT v_order_by->>'column' = 'start_time', 'source_order_by should round-trip';
  RAISE NOTICE 'PASS: create_column_embedding_mapping - mapping %', v_mapping_id;
END $$;

-- ============================================
-- Test 20b: init_column_embedding activates a declared mapping, without
-- creating any physical backing table (unlike init_vector_index)
-- ============================================
DO $$
DECLARE
  v_mapping_id UUID;
  v_index_id UUID;
  v_status dist_rag.column_embedding_registration_status_enum;
  v_linked_index_id UUID;
  v_index_name VARCHAR(50);
  v_ai_provider dist_rag.ai_provider_enum;
  v_embedding_model_params JSONB;
  v_table_exists BOOLEAN;
BEGIN
  RAISE NOTICE '=== Test 20b: init_column_embedding activates a mapping ===';

  SELECT id INTO v_mapping_id FROM dist_rag.column_embedding_registrations
  WHERE registration_name = 'conversation_history_search_langfuse_test';

  v_index_id := dist_rag.init_column_embedding(
    r_mapping_id := v_mapping_id,
    r_ai_provider := 'OPENAI',
    r_embedding_model_params := jsonb_build_object('model', 'text-embedding-3-large', 'dimensions', 1536)
  );
  ASSERT v_index_id IS NOT NULL, 'init_column_embedding should return a vector_index_id';

  SELECT status, vector_index_id INTO v_status, v_linked_index_id
  FROM dist_rag.column_embedding_registrations WHERE id = v_mapping_id;
  ASSERT v_status = 'ACTIVE'::dist_rag.column_embedding_registration_status_enum,
    'init_column_embedding should activate the mapping';
  ASSERT v_linked_index_id = v_index_id, 'mapping.vector_index_id should link to the new vector_indexes row';

  SELECT index_name, ai_provider, embedding_model_params INTO v_index_name, v_ai_provider, v_embedding_model_params
  FROM dist_rag.vector_indexes WHERE id = v_index_id;
  ASSERT v_index_name = 'conversation_history_search_langfuse_test',
    'the new vector_indexes row should reuse the mapping''s registration_name as index_name';
  ASSERT v_ai_provider = 'OPENAI'::dist_rag.ai_provider_enum, 'ai_provider should round-trip';
  ASSERT v_embedding_model_params->>'model' = 'text-embedding-3-large', 'embedding_model_params should round-trip';

  -- The whole point of not calling _create_vector_index_table: no physical
  -- table named after the mapping/index should exist anywhere.
  SELECT EXISTS (
    SELECT 1 FROM pg_catalog.pg_tables
    WHERE tablename = 'conversation_history_search_langfuse_test'
  ) INTO v_table_exists;
  ASSERT NOT v_table_exists,
    'init_column_embedding must never create a physical backing table (destination table already exists)';

  RAISE NOTICE 'PASS: init_column_embedding activates a mapping';
END $$;

-- ============================================
-- Test 20c: init_column_embedding rejects re-initializing an already-active mapping
-- ============================================
DO $$
DECLARE
  v_mapping_id UUID;
  v_error_caught BOOLEAN := FALSE;
BEGIN
  RAISE NOTICE '=== Test 20c: init_column_embedding rejects double-init ===';

  SELECT id INTO v_mapping_id FROM dist_rag.column_embedding_registrations
  WHERE registration_name = 'conversation_history_search_langfuse_test';

  BEGIN
    PERFORM dist_rag.init_column_embedding(
      r_mapping_id := v_mapping_id, r_ai_provider := 'OPENAI',
      r_embedding_model_params := jsonb_build_object('model', 'text-embedding-3-large', 'dimensions', 1536)
    );
  EXCEPTION WHEN OTHERS THEN
    v_error_caught := TRUE;
  END;
  ASSERT v_error_caught, 'Re-initializing an already-active mapping should raise an exception';

  RAISE NOTICE 'PASS: init_column_embedding rejects double-init';
END $$;

-- ============================================
-- Test 20d: init_column_embedding validates mapping_id and embedding_model_params
-- ============================================
DO $$
DECLARE
  v_error_caught BOOLEAN;
  v_pending_mapping_id UUID;
BEGIN
  RAISE NOTICE '=== Test 20d: init_column_embedding validation ===';

  -- Unknown mapping_id
  v_error_caught := FALSE;
  BEGIN
    PERFORM dist_rag.init_column_embedding(
      r_mapping_id := gen_random_uuid(), r_ai_provider := 'OPENAI',
      r_embedding_model_params := jsonb_build_object('dimensions', 1536)
    );
  EXCEPTION WHEN OTHERS THEN
    v_error_caught := TRUE;
  END;
  ASSERT v_error_caught, 'Unknown mapping_id should raise an exception';

  -- Missing "dimensions" in embedding_model_params
  v_pending_mapping_id := dist_rag.create_column_embedding_mapping(
    r_registration_name := 'pending_init_validation_test',
    r_destination_schema := 'datapacks', r_destination_table := 'conversation_history_search',
    r_destination_embedding_column := 'embeddings',
    r_source_schema := 'clickhouse', r_source_table := 'observations',
    r_source_text_columns := ARRAY['input'],
    r_source_filter_by_columns := jsonb_build_array(jsonb_build_object('source_filter_on_column', 'trace_id', 'value', 'x'))
  );
  v_error_caught := FALSE;
  BEGIN
    PERFORM dist_rag.init_column_embedding(
      r_mapping_id := v_pending_mapping_id, r_ai_provider := 'OPENAI',
      r_embedding_model_params := jsonb_build_object('model', 'text-embedding-3-large')
    );
  EXCEPTION WHEN OTHERS THEN
    v_error_caught := TRUE;
  END;
  ASSERT v_error_caught, 'embedding_model_params missing "dimensions" should raise an exception';

  -- That mapping should still be uninitialized after the failed attempt.
  ASSERT (SELECT vector_index_id FROM dist_rag.column_embedding_registrations
          WHERE id = v_pending_mapping_id) IS NULL,
    'a failed init_column_embedding call must not leave a partially-linked mapping';

  RAISE NOTICE 'PASS: init_column_embedding validation';
END $$;

-- ============================================
-- Test 20e: create_column_embedding_mapping is idempotent (ON CONFLICT DO
-- UPDATE) and re-running it never un-initializes an already-active mapping
-- ============================================
DO $$
DECLARE
  v_mapping_id_1 UUID;
  v_mapping_id_2 UUID;
  v_batch_size INTEGER;
  v_count INTEGER;
  v_status dist_rag.column_embedding_registration_status_enum;
  v_vector_index_id UUID;
BEGIN
  RAISE NOTICE '=== Test 20e: create_column_embedding_mapping is idempotent ===';

  v_mapping_id_1 := dist_rag.create_column_embedding_mapping(
    r_registration_name := 'idempotent_test_registration',
    r_destination_schema := 'datapacks', r_destination_table := 'conversation_history_search',
    r_destination_embedding_column := 'embeddings',
    r_source_schema := 'clickhouse', r_source_table := 'observations',
    r_source_text_columns := ARRAY['input'],
    r_source_filter_by_columns := jsonb_build_array(jsonb_build_object('source_filter_on_column', 'trace_id', 'value', 'x')),
    r_destination_claim_batch_size := 25
  );

  PERFORM dist_rag.init_column_embedding(
    r_mapping_id := v_mapping_id_1, r_ai_provider := 'OPENAI',
    r_embedding_model_params := jsonb_build_object('model', 'text-embedding-3-large', 'dimensions', 1536)
  );

  -- Re-declare with a changed value -- should update the existing row (not
  -- duplicate it) and must NOT touch vector_index_id/status.
  v_mapping_id_2 := dist_rag.create_column_embedding_mapping(
    r_registration_name := 'idempotent_test_registration',
    r_destination_schema := 'datapacks', r_destination_table := 'conversation_history_search',
    r_destination_embedding_column := 'embeddings',
    r_source_schema := 'clickhouse', r_source_table := 'observations',
    r_source_text_columns := ARRAY['input'],
    r_source_filter_by_columns := jsonb_build_array(jsonb_build_object('source_filter_on_column', 'trace_id', 'value', 'x')),
    r_destination_claim_batch_size := 500
  );

  ASSERT v_mapping_id_1 = v_mapping_id_2, 'Re-declaring the same name should update, not create a new id';

  SELECT COUNT(*) INTO v_count FROM dist_rag.column_embedding_registrations
  WHERE registration_name = 'idempotent_test_registration';
  ASSERT v_count = 1, 'Re-declaring the same name should not duplicate the row';

  SELECT destination_claim_batch_size, status, vector_index_id
  INTO v_batch_size, v_status, v_vector_index_id
  FROM dist_rag.column_embedding_registrations WHERE id = v_mapping_id_1;
  ASSERT v_batch_size = 500, 'Re-declaring should apply the new config value';
  ASSERT v_status = 'ACTIVE'::dist_rag.column_embedding_registration_status_enum,
    'Re-declaring an already-initialized mapping must not reset its status to PAUSED';
  ASSERT v_vector_index_id IS NOT NULL,
    'Re-declaring an already-initialized mapping must not clear its vector_index_id';

  RAISE NOTICE 'PASS: create_column_embedding_mapping is idempotent';
END $$;

-- ============================================
-- Test 21: create_column_embedding_mapping validates required fields and filter-by-column shape
-- ============================================
DO $$
DECLARE
  v_error_caught BOOLEAN;
BEGIN
  RAISE NOTICE '=== Test 21: create_column_embedding_mapping validation ===';

  -- NULL source_text_columns
  v_error_caught := FALSE;
  BEGIN
    PERFORM dist_rag.create_column_embedding_mapping(
      r_registration_name := 'validation_test_1',
      r_destination_schema := 'datapacks', r_destination_table := 'conversation_history_search',
      r_destination_embedding_column := 'embeddings',
      r_source_schema := 'clickhouse', r_source_table := 'observations',
      r_source_text_columns := NULL,
      r_source_filter_by_columns := jsonb_build_array(jsonb_build_object('source_filter_on_column', 'id', 'value', 'x'))
    );
  EXCEPTION WHEN OTHERS THEN
    v_error_caught := TRUE;
  END;
  ASSERT v_error_caught, 'NULL source_text_columns should raise an exception';

  -- Filter entry with zero value sources
  v_error_caught := FALSE;
  BEGIN
    PERFORM dist_rag.create_column_embedding_mapping(
      r_registration_name := 'validation_test_3',
      r_destination_schema := 'datapacks', r_destination_table := 'conversation_history_search',
      r_destination_embedding_column := 'embeddings',
      r_source_schema := 'clickhouse', r_source_table := 'observations',
      r_source_text_columns := ARRAY['input'],
      r_source_filter_by_columns := jsonb_build_array(jsonb_build_object('source_filter_on_column', 'id'))
    );
  EXCEPTION WHEN OTHERS THEN
    v_error_caught := TRUE;
  END;
  ASSERT v_error_caught, 'Filter entry with no value source should raise an exception';

  -- Filter entry with two value sources (value AND target_value_from_column)
  v_error_caught := FALSE;
  BEGIN
    PERFORM dist_rag.create_column_embedding_mapping(
      r_registration_name := 'validation_test_4',
      r_destination_schema := 'datapacks', r_destination_table := 'conversation_history_search',
      r_destination_embedding_column := 'embeddings',
      r_source_schema := 'clickhouse', r_source_table := 'observations',
      r_source_text_columns := ARRAY['input'],
      r_source_filter_by_columns := jsonb_build_array(
        jsonb_build_object('source_filter_on_column', 'id', 'value', 'x', 'target_value_from_column', 'y')
      )
    );
  EXCEPTION WHEN OTHERS THEN
    v_error_caught := TRUE;
  END;
  ASSERT v_error_caught, 'Filter entry with two value sources should raise an exception';

  -- resolve sub-spec missing where_value/where_value_from_row_column
  v_error_caught := FALSE;
  BEGIN
    PERFORM dist_rag.create_column_embedding_mapping(
      r_registration_name := 'validation_test_5',
      r_destination_schema := 'datapacks', r_destination_table := 'conversation_history_search',
      r_destination_embedding_column := 'embeddings',
      r_source_schema := 'clickhouse', r_source_table := 'observations',
      r_source_text_columns := ARRAY['input'],
      r_source_filter_by_columns := jsonb_build_array(jsonb_build_object('source_filter_on_column', 'project_id', 'resolve',
        jsonb_build_object('schema', 's', 'table', 't', 'select_column', 'c', 'where_column', 'w')))
    );
  EXCEPTION WHEN OTHERS THEN
    v_error_caught := TRUE;
  END;
  ASSERT v_error_caught, 'resolve sub-spec missing where_value/where_value_from_row_column should raise an exception';

  RAISE NOTICE 'PASS: create_column_embedding_mapping validation';
END $$;

-- ============================================
-- Test 22: set_column_embedding_registration_status pauses and resumes
-- ============================================
DO $$
DECLARE
  v_status dist_rag.column_embedding_registration_status_enum;
  v_error_caught BOOLEAN := FALSE;
BEGIN
  RAISE NOTICE '=== Test 22: set_column_embedding_registration_status ===';

  PERFORM dist_rag.set_column_embedding_registration_status('conversation_history_search_langfuse_test', 'PAUSED');
  SELECT status INTO v_status FROM dist_rag.column_embedding_registrations
  WHERE registration_name = 'conversation_history_search_langfuse_test';
  ASSERT v_status = 'PAUSED'::dist_rag.column_embedding_registration_status_enum, 'Status should be PAUSED';

  PERFORM dist_rag.set_column_embedding_registration_status('conversation_history_search_langfuse_test', 'ACTIVE');
  SELECT status INTO v_status FROM dist_rag.column_embedding_registrations
  WHERE registration_name = 'conversation_history_search_langfuse_test';
  ASSERT v_status = 'ACTIVE'::dist_rag.column_embedding_registration_status_enum, 'Status should be ACTIVE again';

  BEGIN
    PERFORM dist_rag.set_column_embedding_registration_status('does_not_exist_registration', 'PAUSED');
  EXCEPTION WHEN OTHERS THEN
    v_error_caught := TRUE;
  END;
  ASSERT v_error_caught, 'Unknown registration_name should raise an exception';

  RAISE NOTICE 'PASS: set_column_embedding_registration_status';
END $$;

-- ============================================
-- Test 23: column_embedding_registration_stats view + column_embedding_registration_progress function
-- ============================================
DO $$
DECLARE
  v_reg_id UUID;
  v_pending INT;
  v_in_progress INT;
  v_failed INT;
  v_total BIGINT;
  v_embedded BIGINT;
  v_pending_or_failed BIGINT;
  v_error_caught BOOLEAN := FALSE;
BEGIN
  RAISE NOTICE '=== Test 23: column_embedding_registration_stats / column_embedding_registration_progress ===';

  SELECT id INTO v_reg_id FROM dist_rag.column_embedding_registrations
  WHERE registration_name = 'conversation_history_search_langfuse_test';

  INSERT INTO dist_rag.column_embedding_progress (registration_id, dest_row_pk_in_text, status)
  VALUES
    (v_reg_id, 'row-pending-1', 'QUEUED'),
    (v_reg_id, 'row-pending-2', 'QUEUED'),
    (v_reg_id, 'row-in-progress-1', 'IN_PROGRESS'),
    (v_reg_id, 'row-failed-1', 'FAILED');

  SELECT pending_count, in_progress_count, failed_count
  INTO v_pending, v_in_progress, v_failed
  FROM dist_rag.column_embedding_registration_stats
  WHERE registration_name = 'conversation_history_search_langfuse_test';

  ASSERT v_pending = 2, 'pending_count should be 2';
  ASSERT v_in_progress = 1, 'in_progress_count should be 1';
  ASSERT v_failed = 1, 'failed_count should be 1';

  SELECT total_rows, embedded_rows, pending_or_failed_rows
  INTO v_total, v_embedded, v_pending_or_failed
  FROM dist_rag.column_embedding_registration_progress('conversation_history_search_langfuse_test');

  ASSERT v_total IS NOT NULL, 'total_rows should be computed against the real destination table';
  ASSERT v_total = v_embedded + v_pending_or_failed,
    'total_rows should equal embedded_rows + pending_or_failed_rows';

  BEGIN
    PERFORM dist_rag.column_embedding_registration_progress('does_not_exist_registration');
  EXCEPTION WHEN OTHERS THEN
    v_error_caught := TRUE;
  END;
  ASSERT v_error_caught, 'Unknown registration_name should raise a clear error';

  RAISE NOTICE 'PASS: column_embedding_registration_stats / column_embedding_registration_progress';
END $$;

-- ============================================
-- Final Test Report
-- ============================================
DO $$
DECLARE
  v_total_sources INT;
  v_total_indexes INT;
  v_total_work_items INT;
  v_total_documents INT;
BEGIN
  RAISE NOTICE '';
  RAISE NOTICE '=== TEST SUMMARY ===';
  SELECT COUNT(*) INTO v_total_sources FROM dist_rag.sources;
  SELECT COUNT(*) INTO v_total_indexes FROM dist_rag.vector_indexes;
  SELECT COUNT(*) INTO v_total_work_items FROM dist_rag.work_queue;
  SELECT COUNT(*) INTO v_total_documents FROM dist_rag.documents;
  RAISE NOTICE 'Total sources created: %', v_total_sources;
  RAISE NOTICE 'Total indexes created: %', v_total_indexes;
  RAISE NOTICE 'Total work queue items: %', v_total_work_items;
  RAISE NOTICE 'Total documents added: %', v_total_documents;
  RAISE NOTICE '';
  RAISE NOTICE '✓ All tests completed successfully!';
  RAISE NOTICE '';
END $$;

