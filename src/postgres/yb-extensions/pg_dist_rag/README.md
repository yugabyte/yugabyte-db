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
| **Column Embedding Registration** | A one-time mapping from a source `table.column` to a destination `table.column`. Once activated, matching destination rows (`embedding_column IS NULL`) are discovered and embedded automatically -- no per-row enqueue call, ever. Independent of `dist_rag.sources`/`dist_rag.documents`. |
| **Column Embedding Progress** | Internal lease/retry tracking for in-flight column-embedding rows, keyed by `(registration, row)` -- never lives on the destination table itself. |

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
| `r_ai_provider` | `ai_provider_enum` | `'OPENAI'` | One of: `OPENAI`, `LOCAL`, `AWS_BEDROCK` |
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

## Column Embedding

A separate, generic capability from the KB-ingestion workflow above: instead of embedding chunks of a crawled document, this embeds a text column of an arbitrary existing table into a vector column of an arbitrary existing (possibly different) table -- registered once, then fully automatic. Mirrors the `create_source` -> `init_vector_index` two-step shape.

### 1. Declare a Mapping

Declares the source -> destination mapping. Not yet discoverable/pollable at this point -- returns a `mapping_id`, but the worker won't touch anything until step 2 activates it.

```sql
SELECT dist_rag.create_column_embedding_mapping(
  r_registration_name := 'my_mapping',
  r_destination_schema := 'public',
  r_destination_table := 'my_table',
  r_destination_embedding_column := 'embeddings',
  r_source_schema := 'public',
  r_source_table := 'my_table',
  r_source_text_columns := ARRAY['content'],
  r_source_filter_by_columns := '[{"source_filter_on_column": "id", "target_value_from_column": "id"}]'::jsonb
) AS mapping_id;
```

**Parameters:**

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `r_registration_name` | `TEXT` | *(required)* | Unique name for this mapping |
| `r_destination_schema` / `r_destination_table` | `TEXT` | *(required)* | Where the embedding gets written |
| `r_destination_embedding_column` | `TEXT` | *(required)* | The nullable vector column to fill in -- the **only** thing the destination table needs to opt in; `embedding_column IS NULL` is the entire discovery signal |
| `r_source_schema` / `r_source_table` | `TEXT` | *(required)* | Where the text to embed is read from -- same table as the destination for a same-table mapping, a different table otherwise |
| `r_source_text_columns` | `TEXT[]` | *(required)* | Column(s) to read and join (with a blank line) into the text that gets embedded |
| `r_source_filter_by_columns` | `JSONB` | *(required)* | How to find the exact source row for a given claimed destination row -- see **Filter entry shapes** below. Must narrow to exactly one row; a mismatch here is the most common source of "wrong text got embedded" bugs |
| `r_destination_pk_column` | `TEXT` | `'id'` | Primary key column on the destination table |
| `r_destination_tenant_column` | `TEXT` | `NULL` | Optional -- per-tenant fairness bucketing during a flush |
| `r_destination_priority_column` | `TEXT` | `NULL` | Optional -- distinguishes live traffic (`0`/`NULL`) from backlog (any other value) for claim ordering; omit entirely if the table has no such column |
| `r_destination_stale_claim_seconds` | `INTEGER` | `900` | How long a claimed-but-unfinished row stays claimed before another worker can reclaim it |
| `r_destination_claim_batch_size` | `INTEGER` | `25` | Rows claimed per registration per poll cycle |
| `r_destination_reserved_backfill_slots` | `INTEGER` | `5` | Rows reserved for the backlog tier every cycle, so live traffic can never fully starve it |
| `r_source_connection` | `TEXT` | `'generic'` | `SOURCE_READERS` registry key -- `'generic'` covers every source today |
| `r_source_order_by` | `JSONB` | `NULL` | `{"column": ..., "direction": "ASC"\|"DESC"}` -- only needed if a filter can match more than one source row |
| `r_created_by` | `TEXT` | `NULL` | Free-text attribution |

**Returns:** `UUID` -- the mapping ID.

**Idempotent:** calling again with the same `r_registration_name` updates that mapping's config (`ON CONFLICT DO UPDATE`) instead of duplicating it -- e.g. to bump `r_destination_claim_batch_size` later. It never touches `vector_index_id`/`status`, so re-declaring can't accidentally un-initialize an already-active mapping.

#### Filter entry shapes

`r_source_filter_by_columns` is a JSON array, ANDed together. Each entry needs a `source_filter_on_column` (which column, *in the source table*, to filter on) plus **exactly one** of the following to supply the comparison value:

| Key | Meaning |
|-----|---------|
| `value` | A literal, fixed at registration time |
| `target_value_from_column` | Take the value from *this column on the claimed destination row* -- what makes one mapping apply correctly to every row it discovers, not just one specific row |
| `resolve` | An indirect lookup first (see below) -- for when the value itself has to be resolved through another table |

```jsonc
// same-table mapping: match the source row by the claimed row's own id
{"source_filter_on_column": "id", "target_value_from_column": "id"}

// cross-table mapping: source column name differs from the destination's
{"source_filter_on_column": "trace_id", "target_value_from_column": "conversation_id"}

// JSONB path: metadata->>'message_id' = <value>
{"source_filter_on_column": "metadata", "json_key": "message_id", "target_value_from_column": "message_id"}

// indirect resolver: value comes from a lookup, itself keyed by the row
{"source_filter_on_column": "project_id", "resolve": {
  "schema": "meko_system", "table": "some_mapping_table",
  "select_column": "external_project_id", "where_column": "internal_id",
  "where_value_from_row_column": "tenant_id"
}}
```

> **Note:** `source_filter_on_column` and `target_value_from_column` name columns on two *different* tables (source vs. the claimed destination row) and are independent -- they only happen to match in the first example because source and destination are the same table there. Getting this wrong (e.g. swapping them, or pointing `target_value_from_column` at a column the destination table doesn't actually have) is caught immediately: `create_column_embedding_mapping` validates the shape at registration time (`RAISE EXCEPTION` on a malformed entry), and a genuinely missing row column raises loudly at read time rather than silently embedding the wrong text.

### 2. Activate the Mapping

Supplies the embedding-model config, creates the `dist_rag.vector_indexes` row for it (reusing that table purely as model-config storage), and activates the mapping. **Unlike `init_vector_index`, this never creates a physical table** -- the destination table already exists.

```sql
SELECT dist_rag.init_column_embedding(
  r_mapping_id := '<mapping_id from step 1>'::uuid,
  r_ai_provider := 'OPENAI',
  r_embedding_model_params := '{"model": "text-embedding-3-large", "dimensions": 1536}'::jsonb
) AS vector_index_id;
```

**Parameters:**

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `r_mapping_id` | `UUID` | *(required)* | The mapping returned by step 1 |
| `r_ai_provider` | `ai_provider_enum` | *(required)* | One of: `OPENAI`, `LOCAL`, `AWS_BEDROCK` |
| `r_embedding_model_params` | `JSONB` | *(required)* | Must contain `"dimensions"` (same requirement as `init_vector_index`) |

**Returns:** `UUID` -- the new `vector_indexes` row's ID.

> **Note:** Raises if the mapping is unknown, or if it's already initialized (has a `vector_index_id`) -- use `set_column_embedding_registration_status` to pause/resume an already-active mapping instead of re-initializing it.

Once activated, there is **no `build_index`-equivalent step** -- the worker's continuous poll loop *is* the perpetual build. From here on, any row already in the destination table with `embeddings IS NULL`, and any new row inserted going forward, is discovered and embedded automatically.

### 3. Monitor a Mapping

```sql
-- In-flight/failed progress-table counts (cheap; can't show a "done" count -- see below)
SELECT * FROM dist_rag.column_embedding_registration_stats
WHERE registration_name = 'my_mapping';

-- Total/embedded/pending-or-failed counts against the real destination table
-- (a real COUNT(*) query -- fine every few minutes, not something to poll every few seconds)
SELECT * FROM dist_rag.column_embedding_registration_progress('my_mapping');
```

`column_embedding_registration_stats` can't show a "done" count -- a successfully embedded row's progress entry is deleted by design (its `embedding_column` no longer being `NULL` is itself the completion signal). Use `column_embedding_registration_progress(name)` for that.

### 4. Pause / Resume a Mapping

```sql
SELECT dist_rag.set_column_embedding_registration_status('my_mapping', 'PAUSED');
SELECT dist_rag.set_column_embedding_registration_status('my_mapping', 'ACTIVE');
```

Pauses/resumes without deleting the mapping's configuration.

> **Note:** A registration this size and its worker (`WORKER_TYPE=AUTO_COLUMN_EMBEDDING`) also require the destination table to have a partial index matching the discovery predicate (`WHERE {embedding_column} IS NULL`) before registering against a table with a non-trivial row count -- otherwise every poll cycle is a full table scan. See the `rag-worker-conversation-embedding` design doc for the large-table backfill considerations.

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
| `dist_rag.column_embedding_registrations` | One row per declared/activated column-embedding mapping |
| `dist_rag.column_embedding_progress` | In-flight/failed lease-retry state for column embedding, keyed by `(registration_id, dest_row_pk_in_text)` -- `dest_row_pk_in_text` is the destination row's own PK value, as text. This tracking lives in its own table; it never adds columns to the user's destination table |

### Enum Types

| Type | Values |
|------|--------|
| `secrets_provider_enum` | `LOCAL`, `AWS`, `GCP`, `AZURE`, `HASHICORP_VAULT` |
| `create_source_status_enum` | `QUEUED`, `IN_PROGRESS`, `COMPLETED`, `FAILED` |
| `ai_provider_enum` | `OPENAI`, `LOCAL`, `AWS_BEDROCK` |
| `index_build_status` | `INIT`, `IN_PROGRESS`, `NOT_STARTED` |
| `document_processing_status_enum` | `NOT_STARTED`, `QUEUED`, `PROCESSING`, `COMPLETED`, `FAILED`, `RETRY` |
| `pipeline_status_enum` | `PROCESSING`, `COMPLETED`, `FAILED` |
| `task_type_enum` | `CREATE_SOURCE`, `PREPROCESS` |
| `task_queue_status_enum` | `QUEUED`, `IN_PROGRESS`, `COMPLETED`, `FAILED` |
| `column_embedding_registration_status_enum` | `ACTIVE`, `PAUSED` |

### Views

| View | Description |
|------|-------------|
| `dist_rag.vector_index_pipeline_details` | Detailed per-document pipeline execution info across all indexes |
| `dist_rag.pipeline_stats` | Aggregated pipeline statistics per document per index |
| `dist_rag.column_embedding_registration_stats` | In-flight/failed progress-table counts per column-embedding registration |

## Running Tests

```bash
psql -U postgres -d testdb -f sql/pg_dist_rag_test.sql
```

The test suite validates all public functions, error handling, view correctness, and schema integrity.
