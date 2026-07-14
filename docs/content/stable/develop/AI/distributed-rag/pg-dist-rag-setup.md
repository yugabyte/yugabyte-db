---
title: Distributed RAG setup
headerTitle: Distributed RAG setup
linkTitle: Setup
description: Set up distributed RAG pipelines with integrated embedding generation in YugabyteDB
headcontent: Set up distributed RAG pipelines with integrated embedding generation in YugabyteDB
menu:
  stable_develop:
    identifier: pg-dist-rag-setup
    parent: distributed-rag
    weight: 10
rightNav:
  hideH4: true
type: docs
---

Distributed RAG consists of two components:

- **pg_dist_rag PostgreSQL extension**

  Manages Retrieval-Augmented Generation (RAG) pipelines from SQL. It registers document sources (such as S3 buckets or URLs), coordinates distributed preprocessing and embedding generation, and stores vectors in [pgvector](../../../../additional-features/pg-extensions/extension-pgvector/) indexes backed by YugabyteDB.

  With pg_dist_rag, you can do the following:

  - Point a vector index at a document source instead of building custom ETL pipelines.
  - Chunk documents and generate embeddings using a configured AI provider.
  - Monitor pipeline progress and retry failed documents from SQL views.
  - Query generated embeddings with standard pgvector similarity search.

- **RAG workers**

  Long-running Python services that do the heavy lifting: crawling sources, parsing, chunking, and generating embeddings. Workers run outside the database, on dedicated nodes or VMs, so pipeline failures and resource spikes never affect the database itself. AI workloads and database workloads scale independently.

## Set up YugabyteDB

### Prerequisites

- YugabyteDB {{<release "2025.2">}} or later
- [pgvector](../../../../additional-features/pg-extensions/extension-pgvector/) extension (`vector` type support)
- An OpenAI API key
- Cloud credentials (AWS S3) when reading documents from object storage

### Enable the extension

Install pgvector first, then create the pg_dist_rag extension:

```sql
CREATE EXTENSION IF NOT EXISTS vector;
CREATE EXTENSION IF NOT EXISTS pg_dist_rag;
```

This creates the `dist_rag` schema with tables, types, functions, and views for managing sources, indexes, documents, pipelines, and the work queue.

### Build a vector index

The typical workflow has four steps: create sources, initialize a vector index, optionally add more sources, and build the index, all managed using the following four functions.

| Function | Description |
| :--- | :--- |
| dist_rag.create_source() | Register a document collection by URI (S3 bucket), with optional metadata tags for filtering. |
| dist_rag.init_vector_index() | Create a named vector index backed by a pgvector table, specifying your AI provider and embedding dimensions. |
| dist_rag.add_source_to_index() | Attach one or more sources to an index, with per-source chunking parameters. |
| dist_rag.build_index() | Enqueue the full preprocessing pipeline for all documents in the index. |

#### 1. Create a source

Register a document source URI. This also queues a `CREATE_SOURCE` task in the work queue.

```sql
-- Minimal: just a URI
SELECT dist_rag.create_source(
  r_source_uri := 's3://my-bucket/documents/'
);
```

| Parameter | Type | Default | Description |
| :-------- | :--- | :------ | :---------- |
| `r_source_uri` | `TEXT` | *(required)* | URI of the document source. |
| `r_metadata` | `JSONB` | `'{}'` | Arbitrary metadata for filtering. |
| `r_tenant_id` | `UUID` | `NULL` | Optional tenant identifier for multi-tenant isolation. |

Returns a `UUID` source ID.

#### 2. Initialize a vector index

Create a named vector index, optionally associating it with existing sources. This creates a backing table in the target schema and an HNSW index on the embeddings column.

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

| Parameter | Type | Default | Description |
| :-------- | :--- | :------ | :---------- |
| `r_index_name` | `VARCHAR(50)` | `'pg_rag_default_store'` | Unique name for the index and its backing table. |
| `r_sources` | `UUID[]` | `ARRAY[]::UUID[]` | Source IDs to associate with the index. |
| `r_chunk_params` | `JSONB` | `'{}'` | Chunking configuration for all attached sources. |
| `r_ai_provider` | `ai_provider_enum` | `'OPENAI'` | `OPENAI`. |
| `r_embedding_model_params` | `JSONB` | `'{}'` | Embedding model configuration. Must include a `"dimensions"` key (for example, `{"dimensions": 1536}`). |
| `r_index_options` | `JSONB` | `'{"distance_metric": "cosine", "m": 16, "ef_construction": 64}'` | HNSW index options. `distance_metric` can be `cosine`, `l2`, or `ip`. |
| `r_schema_name` | `VARCHAR(50)` | `'public'` | Schema for the backing vector table. The schema must already exist. |

Returns a `UUID` vector index ID.

{{< note title="Embedding dimensions" >}}
The `r_embedding_model_params` JSONB must include a `"dimensions"` key with a positive integer. The extension creates a `vector(N)` column in the backing table using this value.
{{< /note >}}

#### 3. Add a source to an existing index

Use `add_source_to_index()` to attach additional sources to an already-created vector index, optionally with custom chunking parameters.

```sql
SELECT dist_rag.add_source_to_index(
  r_index_id := '<index_uuid>',
  r_source_id := '<source_uuid>',
  r_chunk_params := '{"chunk_size": 512, "chunk_overlap": 50, "strategy": "recursive"}'::jsonb
);
```

#### 4. Build the index

Kick off preprocessing for all documents across all sources in an index. Each document gets a `PREPROCESS` task queued in the work queue. RAG agent workers claim tasks from the queue and process documents in parallel.

Provide exactly one of `r_index_id` or `r_index_name`:

```sql
SELECT dist_rag.build_index(r_index_id := '<index_uuid>');
```

```sql
SELECT dist_rag.build_index(r_index_name := 'my_knowledge_base');
```

### Schema reference

#### Tables

| Table | Description |
| :---- | :---------- |
| `dist_rag.sources` | Registered document sources. |
| `dist_rag.vector_indexes` | Vector index metadata. |
| `dist_rag.vector_index_source_mappings` | Many-to-many mapping between indexes and sources. |
| `dist_rag.documents` | Individual documents belonging to sources. |
| `dist_rag.pipeline_details` | Per-document pipeline execution records. |
| `dist_rag.work_queue` | Internal task queue with lease-based locking. |

#### Enum types

| Type | Values |
| :--- | :----- |
| `secrets_provider_enum` | `LOCAL`, `AWS`, `GCP`, `AZURE`, `HASHICORP_VAULT` |
| `create_source_status_enum` | `QUEUED`, `IN_PROGRESS`, `COMPLETED`, `FAILED` |
| `ai_provider_enum` | `OPENAI` |
| `index_build_status` | `INIT`, `IN_PROGRESS`, `NOT_STARTED` |
| `document_processing_status_enum` | `NOT_STARTED`, `QUEUED`, `PROCESSING`, `COMPLETED`, `FAILED`, `RETRY` |
| `pipeline_status_enum` | `PROCESSING`, `COMPLETED`, `FAILED` |
| `task_type_enum` | `CREATE_SOURCE`, `PREPROCESS` |
| `task_queue_status_enum` | `QUEUED`, `IN_PROGRESS`, `COMPLETED`, `FAILED` |

#### Views

| View | Description |
| :--- | :---------- |
| `dist_rag.vector_index_pipeline_details` | Detailed per-document pipeline execution info across all indexes. |
| `dist_rag.pipeline_stats` | Aggregated pipeline statistics per document per index. |

## Deploy RAG workers

### Prerequisites

- Network connectivity to the YugabyteDB database (port 5433), S3, and the embedding provider API
- Python 3.11
- For PDF workers, the OCR system packages. For example, on Ubuntu: tesseract-ocr, poppler-utils, and libpq-dev.

### Setup

Create the pg_dist_rag extension before starting workers. Workers poll the `dist_rag.work_queue` table, which is created by CREATE EXTENSION pg_dist_rag.

To deploy a worker:

1. Change to the install directory:

    ```sh
    cd <yugabytedb-install-dir>/python/ai/rag_agent
    ```

1. Create a virtual environment and install dependencies:

    ```sh
    uv venv --python=3.11
    source .venv/bin/activate
    uv pip install -r requirements.txt
    ```

1. Configure the worker:

    ```sh
    export YUGABYTEDB_CONNECTION_STRING="postgresql://yugabyte:<password>@<node-address>:5433/yugabyte"
    export OPENAI_API_KEY=<your-openai-api-key>
    export AWS_REGION=us-east-1
    export AWS_ACCESS_KEY_ID=<key-id>           # omit for public buckets
    export AWS_SECRET_ACCESS_KEY=<secret-key>   # omit for public buckets
    ```

1. Start the worker:

    ```sh
    python __main__.py
    ```

    To start a PDF worker, additionally set the worker type _before_ starting:

    ```sh
    export WORKER_DOCUMENT_TYPE=PDF
    ```

### Environment variables

Workers are configured entirely using the following environment variables.

| Variable | Description | Default |
| :--- | :--- | :--- |
| YUGABYTEDB_CONNECTION_STRING | (Required) Connection string the worker uses to poll the work queue and write metadata and embeddings. | |
| OPENAI_API_KEY | (Required) API key for the OpenAI embedding provider. Managed by the worker; never exposed in SQL. | |
| AWS_ACCESS_KEY_ID<br>AWS_SECRET_ACCESS_KEY<br>AWS_REGION | Credentials and region for reading S3 sources. Publicly readable objects work without credentials. | |
| WORKER_DOCUMENT_TYPE | Worker specialization: PDF claims only PDF documents; TEXT claims all other supported types. | TEXT |
| TASK_LEASE_DURATION | Lease duration, in seconds, for a claimed work-queue task. | 600 |
| POLL_IDLE_SLEEP_SECONDS | Time to sleep between polls when the work queue is empty. | 1 |
| POLL_ERROR_BACKOFF_SECONDS | Backoff, in seconds, after an error in the polling loop. | 60 |

### Scale workers

Ingestion throughput scales horizontally with the number of workers: each worker claims a different document from the work queue, so adding workers increases parallelism with no configuration changes.

Because workers are decoupled from the database, you can:

- Run zero workers when no ingestion is happening; queued work resumes when a worker starts.
- Add TEXT and PDF workers independently, sized to the mix of documents in your sources.
- Place PDF workers on GPU-optimized instances without changing your database node shapes.

## Monitor pipelines

Two views are available for observing pipeline progress and statistics.

Detailed per-document pipeline status:

```sql
SELECT index_name, document_name, pipeline_status, chunks_processed,
       embeddings_persisted, current_step, last_error_message
FROM dist_rag.vector_index_pipeline_details
WHERE index_name = 'my_knowledge_base';
```

Aggregated stats per document:

```sql
SELECT index_name, document_name, calls, total_chunks_processed,
       total_embeddings_persisted, completion_rate_percent
FROM dist_rag.pipeline_stats
WHERE index_name = 'my_knowledge_base';
```

## Query embeddings

After a pipeline completes, embeddings are stored in the backing table created for the vector index (for example, `public.my_knowledge_base`). Query it using standard [pgvector](../../../../additional-features/pg-extensions/extension-pgvector/) operators:

```sql
SELECT id, chunk_text, metadata_filters,
       embeddings <=> '[0.1, 0.2, 0.3]'::vector AS distance
FROM public.my_knowledge_base
ORDER BY embeddings <=> '[0.1, 0.2, 0.3]'::vector
LIMIT 10;
```

Each row includes:

| Column | Description |
| :----- | :---------- |
| `chunk_text` | Text content of the document chunk. |
| `embeddings` | Vector embedding for the chunk. |
| `document_id` | Reference to the source document. |
| `tenant_id` | Tenant identifier, if set on the source. |
| `metadata_filters` | JSONB metadata for relational filtering alongside vector search. |

Combine metadata filters with vector similarity search:

```sql
SELECT chunk_text, embeddings <=> $1 AS distance
FROM public.my_knowledge_base
WHERE metadata_filters @> '{"type": "documentation"}'::jsonb
ORDER BY distance
LIMIT 10;
```

## Complete example

```sql
CREATE EXTENSION IF NOT EXISTS vector;
CREATE EXTENSION IF NOT EXISTS pg_dist_rag;

-- Create document sources
SELECT dist_rag.create_source(
  r_source_uri := 's3://company-docs/engineering/',
  r_metadata := '{"team": "engineering", "access": "internal"}'::jsonb
) AS eng_source_id;

-- Initialize a vector index with both sources
SELECT dist_rag.init_vector_index(
  r_index_name := 'engineering_kb',
  r_sources := ARRAY['a1b2c3d4-abcd-abcd-abcd-abcdefabcdef', 'e5f6a7b8-abcd-abcd-abcd-abcdefabcdef']::UUID[],
  r_ai_provider := 'OPENAI',
  r_embedding_model_params := '{"dimensions": 1536, "model": "text-embedding-ada-002"}'::jsonb
);

-- Build the index (queues all documents for preprocessing)
SELECT dist_rag.build_index(r_index_name := 'engineering_kb');

-- Monitor progress
SELECT index_name, document_name, pipeline_status, chunks_processed, current_step
FROM dist_rag.vector_index_pipeline_details
WHERE index_name = 'engineering_kb';

SELECT document_name, calls, total_chunks_processed, completion_rate_percent
FROM dist_rag.pipeline_stats
WHERE index_name = 'engineering_kb';
```

## Learn more

- [pg_dist_rag extension](https://github.com/yugabyte/yugabyte-db/blob/master/src/postgres/yb-extensions/pg_dist_rag/README.md)
- [RAG agent source code](https://github.com/yugabyte/yugabyte-db/tree/master/python/ai/rag_agent)
