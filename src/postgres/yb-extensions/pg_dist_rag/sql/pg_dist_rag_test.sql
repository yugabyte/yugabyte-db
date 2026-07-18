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
  RAISE NOTICE 'PASS: pipeline_stats - chunks: %, completion rate: %%', v_total_chunks, v_completion_rate;
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

