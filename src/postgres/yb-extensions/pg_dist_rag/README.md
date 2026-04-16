# pg_dist_rag

A PostgreSQL extension for distributed Retrieval-Augmented Generation (RAG) pipelines. It manages document sources, vector indexes, embedding pipelines, and work queues -- all from within SQL.

## Prerequisites

- PostgreSQL with the `pgvector` extension installed (`vector` type support)
- The `pg_dist_rag` extension installed in your database

## Installation

```sql
CREATE EXTENSION IF NOT EXISTS pg_dist_rag;
```

This creates the `dist_rag` schema with all tables, types, functions, and views.

## Core Concepts

| Concept | Description |
|---------|-------------|
| **Source** | A pointer to a collection of documents (e.g. an S3 bucket, a URL). |
| **Vector Index** | A named index that stores embeddings for documents from one or more sources. |
| **Document** | An individual file tracked under a source, processed through the pipeline. |
| **Pipeline** | The processing workflow that chunks documents and generates embeddings. |
| **Work Queue** | Internal task queue that coordinates source creation and document preprocessing. |

## Usage

### 1. Create a Source

Register a document source URI. This also queues a `CREATE_SOURCE` task in the work queue.

```sql
-- Minimal: just a URI
SELECT dist_rag.create_source(
  r_source_uri := 's3://my-bucket/documents/'
);

-- With metadata and cloud secrets provider
SELECT dist_rag.create_source(
  r_source_uri := 's3://my-bucket/documents/',
  r_metadata := '{"language": "english", "type": "documentation"}'::jsonb,
  r_secrets_provider := 'AWS',
  r_secrets_provider_params := '{"api_key": "secret123", "region": "us-east-1"}'::jsonb
);
```

**Parameters:**

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `r_source_uri` | `TEXT` | *(required)* | URI of the document source |
| `r_metadata` | `JSONB` | `'{}'` | Arbitrary metadata for filtering |
| `r_secrets_provider` | `secrets_provider_enum` | `'LOCAL'` | One of: `LOCAL`, `AWS`, `GCP`, `AZURE`, `HASHICORP_VAULT` |
| `r_secrets_provider_params` | `JSONB` | `'{}'` | Provider-specific credentials/config |

**Returns:** `UUID` -- the source ID.

### 2. Initialize a Vector Index

Create a named vector index, optionally associating it with existing sources. This also creates a dynamic backing table to store embeddings.

```sql
-- Create an index with sources attached
SELECT dist_rag.init_vector_index(
  r_index_name := 'my_knowledge_base',
  r_sources := ARRAY['<source_uuid_1>', '<source_uuid_2>']::UUID[],
  r_embedding_model_params := '{"dimensions": 1536}'::jsonb,
  r_ai_provider := 'OPENAI'
);

-- Create an empty index (add sources later)
SELECT dist_rag.init_vector_index(
  r_index_name := 'my_empty_index',
  r_embedding_model_params := '{"dimensions": 1536}'::jsonb
);
```

**Parameters:**

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `r_index_name` | `VARCHAR(50)` | `'pg_rag_default_store'` | Unique name for the index |
| `r_sources` | `UUID[]` | `ARRAY[]::UUID[]` | Source IDs to associate |
| `r_chunk_params` | `JSONB` | `'{}'` | Chunking configuration for all attached sources |
| `r_ai_provider` | `ai_provider_enum` | `'OPENAI'` | One of: `OPENAI`, `LOCAL` |
| `r_embedding_model_params` | `JSONB` | `'{}'` | Must contain `"dimensions"` key (e.g. `{"dimensions": 1536}`) |

**Returns:** `UUID` -- the vector index ID.

> **Note:** The `embedding_model_params` JSONB **must** include a `"dimensions"` key with a positive integer value. The extension creates a `vector(N)` column in the backing table using this value.

### 3. Add a Source to an Existing Index

Attach additional sources to an already-created vector index, optionally with custom chunking parameters.

```sql
-- Add with default chunk params
SELECT dist_rag.add_source_to_index(
  r_index_id := '<index_uuid>',
  r_source_id := '<source_uuid>'
);

-- Add with custom chunk params
SELECT dist_rag.add_source_to_index(
  r_index_id := '<index_uuid>',
  r_source_id := '<source_uuid>',
  r_chunk_params := '{"chunk_size": 512, "overlap": 50, "strategy": "recursive"}'::jsonb
);
```

**Parameters:**

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `r_index_id` | `UUID` | *(required)* | The vector index to add the source to |
| `r_source_id` | `UUID` | *(required)* | The source to attach |
| `r_chunk_params` | `JSONB` | `'{}'` | Chunking configuration for this source |

### 4. Build the Index

Kick off the preprocessing pipeline for all documents across all sources in an index. Each document gets a `PREPROCESS` task queued in the work queue.

```sql
-- Build by index ID
SELECT dist_rag.build_index(r_index_id := '<index_uuid>');

-- Build by index name
SELECT dist_rag.build_index(r_index_name := 'my_knowledge_base');
```

**Parameters (provide exactly one):**

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `r_index_id` | `UUID` | `NULL` | The index ID |
| `r_index_name` | `VARCHAR(50)` | `NULL` | The index name |

> **Note:** You must provide **either** `r_index_id` or `r_index_name`, not both.

### 5. Monitor Pipelines

Two views are available for observing pipeline progress and statistics.

#### Detailed Pipeline View

```sql
-- All pipeline details across all indexes
SELECT * FROM dist_rag.vector_index_pipeline_details;

-- Filter by a specific index
SELECT * FROM dist_rag.vector_index_pipeline_details
WHERE index_name = 'my_knowledge_base';
```

Columns include: `index_id`, `index_name`, `ai_provider`, `source_uri`, `document_name`, `document_status`, `pipeline_status`, `chunks_processed`, `embeddings_persisted`, `current_step`, `last_error_message`, and timestamps.

#### Aggregated Pipeline Stats

```sql
-- Summary stats per document across all indexes
SELECT * FROM dist_rag.pipeline_stats;

-- Filter by a specific index
SELECT index_name, document_name, calls, total_chunks_processed,
       total_embeddings_persisted, completion_rate_percent
FROM dist_rag.pipeline_stats
WHERE index_name = 'my_knowledge_base';
```

Columns include: `index_id`, `index_name`, `document_name`, `calls`, `total_chunks_processed`, `total_embeddings_persisted`, `total_exec_time_seconds`, `successful_completions`, `completion_rate_percent`, and more.

## Complete Example

```sql
-- Step 1: Install the extension
CREATE EXTENSION IF NOT EXISTS pg_dist_rag;

-- Step 2: Create document sources
SELECT dist_rag.create_source(
  r_source_uri := 'https://docs.example.com/api-reference/'
) AS api_source_id;
-- returns: e.g. 'a1b2c3d4-...'

SELECT dist_rag.create_source(
  r_source_uri := 's3://company-docs/engineering/',
  r_metadata := '{"team": "engineering", "access": "internal"}'::jsonb,
  r_secrets_provider := 'AWS',
  r_secrets_provider_params := '{"region": "us-east-1"}'::jsonb
) AS eng_source_id;
-- returns: e.g. 'e5f6g7h8-...'

-- Step 3: Initialize a vector index with both sources
SELECT dist_rag.init_vector_index(
  r_index_name := 'engineering_kb',
  r_sources := ARRAY['a1b2c3d4-...', 'e5f6g7h8-...']::UUID[],
  r_ai_provider := 'OPENAI',
  r_embedding_model_params := '{"dimensions": 1536, "model": "text-embedding-ada-002"}'::jsonb
);

-- Step 4: Build the index (queues all documents for preprocessing)
SELECT dist_rag.build_index(r_index_name := 'engineering_kb');

-- Step 5: Monitor progress
SELECT index_name, document_name, pipeline_status, chunks_processed, current_step
FROM dist_rag.vector_index_pipeline_details
WHERE index_name = 'engineering_kb';

-- Step 6: Check overall stats
SELECT document_name, calls, total_chunks_processed, completion_rate_percent
FROM dist_rag.pipeline_stats
WHERE index_name = 'engineering_kb';
```

## Schema Reference

### Tables

| Table | Description |
|-------|-------------|
| `dist_rag.sources` | Registered document sources |
| `dist_rag.vector_indexes` | Vector index metadata |
| `dist_rag.vector_index_source_mappings` | Many-to-many mapping between indexes and sources |
| `dist_rag.documents` | Individual documents belonging to sources |
| `dist_rag.pipeline_details` | Per-document pipeline execution records |
| `dist_rag.work_queue` | Internal task queue with lease-based locking |

### Enum Types

| Type | Values |
|------|--------|
| `secrets_provider_enum` | `LOCAL`, `AWS`, `GCP`, `AZURE`, `HASHICORP_VAULT` |
| `create_source_status_enum` | `QUEUED`, `IN_PROGRESS`, `COMPLETED`, `FAILED` |
| `ai_provider_enum` | `OPENAI`, `LOCAL` |
| `index_build_status` | `INIT`, `IN_PROGRESS`, `NOT_STARTED` |
| `document_processing_status_enum` | `NOT_STARTED`, `QUEUED`, `PROCESSING`, `COMPLETED`, `FAILED`, `RETRY` |
| `pipeline_status_enum` | `PROCESSING`, `COMPLETED`, `FAILED` |
| `task_type_enum` | `CREATE_SOURCE`, `PREPROCESS` |
| `task_queue_status_enum` | `QUEUED`, `IN_PROGRESS`, `COMPLETED`, `FAILED` |

### Views

| View | Description |
|------|-------------|
| `dist_rag.vector_index_pipeline_details` | Detailed per-document pipeline execution info across all indexes |
| `dist_rag.pipeline_stats` | Aggregated pipeline statistics per document per index |

## Running Tests

```bash
psql -U postgres -d testdb -f sql/pg_dist_rag_test.sql
```

The test suite validates all public functions, error handling, view correctness, and schema integrity.
