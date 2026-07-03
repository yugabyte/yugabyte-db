---
title: pg_dist_rag extension
headerTitle: pg_dist_rag extension
linkTitle: pg_dist_rag
description: Build distributed RAG pipelines with integrated embedding generation in YugabyteDB
tags:
  feature: tech-preview
menu:
  stable:
    identifier: extension-pg-dist-rag
    parent: pg-extensions
    weight: 20
type: docs
---

{{<tags/feature/tp idea="2537">}}The [pg_dist_rag](https://github.com/yugabyte/yugabyte-db/blob/master/src/postgres/yb-extensions/pg_dist_rag/README.md) PostgreSQL extension manages Retrieval-Augmented Generation (RAG) pipelines from SQL. It registers document sources (such as S3 buckets or URLs), coordinates distributed preprocessing and embedding generation, and stores vectors in [pgvector](../extension-pgvector/) indexes backed by YugabyteDB.

With pg_dist_rag, you can do the following:

- Point a vector index at a document source instead of building custom ETL pipelines.
- Chunk documents and generate embeddings using a configured AI provider.
- Monitor pipeline progress and retry failed documents from SQL views.
- Query generated embeddings with standard pgvector similarity search.

## Overview

RAG applications get a lot of attention for what happens at query time: vector similarity search, prompt assembly, LLM invocation. The harder operational problem is what happens before that; that is, the preprocessing pipeline that transforms raw documents into searchable vector embeddings.

In most implementations, that pipeline lives entirely outside the database. A Python worker fetches documents from S3 or a URL, passes them through a parsing and chunking library, calls an embedding API, and finally inserts results into pgvector. Each step has its own failure modes, its own retry semantics, and its own monitoring story. When a document fails to embed, you need to know which step it failed at and whether it should be retried. When your document corpus grows to millions of files across diverse formats, you need to know how far the pipeline has gotten and whether it's keeping up with incoming changes.

Typically this infrastructure is built from scratch. At production scale, across diverse source types and formats, this can become a significant ongoing operational burden requiring hundreds to thousands of lines of code to write, test, deploy, and keep current.

pg_dist_rag manages this pipeline through SQL inside the YugabyteDB database; compute-heavy preprocessing runs in decoupled RAG workers, isolated from the database process.

The extension manages a work queue internally. Documents are processed asynchronously (parsed, chunked, and embedded using the configured provider) and results land directly in the pgvector-backed index table, queryable with standard SQL the moment each document completes.

A typical "Hello RAG" implementation requires over 100 lines of Python to handle a single directory of documents. With pg_dist_rag, the equivalent is a single function call:

```sql
SELECT dist_rag.create_source('s3://company-docs/engineering/');
```

pg_dist_rag is anchored in YugabyteDB's distributed SQL architecture. The same cluster that handles transactional workloads manages the ingestion pipeline: horizontally scalable, geo-distributed for data residency requirements, and resilient to node and availability zone failures.

### Data sources

pg_dist_rag is designed as a unified data access broker. It currently supports ingestion of PDF, text, JSON, CSV, HTML, XML, and markdown, as well as S3 source locations; S3 and OpenAI credentials are stored as RAG worker environment variables.

For data that is too large or costly to move, YugabyteDB can query it in place, eliminating the data movement cost that often makes large-scale RAG pipelines expensive and operationally complex. The result is less data to move, less storage to manage, and a reduction in the operational cost of keeping multiple systems in sync.

Parsing, chunking, and embedding are handled using a comprehensive tool stack (LangChain and Unstructured.io) giving teams flexibility at every stage of the pipeline without having to integrate those tools themselves.

### Observability

Two views surface pipeline state, providing visibility into your embedding pipeline without any external tooling:

- dist_rag.vector_index_pipeline_details gives a per-document breakdown: which pipeline step is active, how many chunks have been processed, how many embeddings persisted, and the last error message if processing failed.
- dist_rag.pipeline_stats gives aggregate statistics per document per index: total chunks processed, total embeddings persisted, completion rate, and cumulative execution time.

### Multi-tenancy

pg_dist_rag is multi-tenant capable. Each source carries an optional tenant_id, giving each tenant a fully isolated pipeline namespace. Teams building multi-tenant RAG applications, where one database cluster serves many customers' document collections, can use a single installation across all tenants without cross-tenant data leakage.

### Core concepts

| Concept | Description |
| :------ | :---------- |
| Source | A pointer to a collection of documents (for example, an S3 bucket prefix or URL). |
| Vector index | A named index that stores embeddings for documents from one or more sources. Each index has a backing table with an HNSW vector index. |
| Document | An individual file tracked under a source and processed through the pipeline. |
| Pipeline | The workflow that chunks a document and generates embeddings for each chunk. |
| Work queue | Internal task queue (`dist_rag.work_queue`) that coordinates source creation and document preprocessing across RAG workers. |

## Prerequisites

- YugabyteDB {{<release "2025.2">}} or later.
- The [pgvector](../extension-pgvector/) extension (`vector` type support).
- An OpenAI API key.
- Cloud credentials (for example, AWS S3) when reading documents from object storage.
- A VM for RAG workers with
  - network connectivity to the YugabyteDB database (port 5433), S3, and the embedding API.
  - Python 3.11.

## Set up pg_dist_rag

### Start the RAG service

pg_dist_rag relies on a Python RAG agent that runs on a separate node. The agent polls the `dist_rag.work_queue` table, processes documents, and writes embeddings to dynamically created vector tables.

Before you enable the extension, start the RAG agent service on all YB-TServers:

1. Set the `enable_pg_dist_rag_service` [yb-tserver](../../../reference/configuration/yb-tserver/) flag to true.
1. Optionally set the `pg_dist_rag_conf_csv` flag to supply service-level credentials and configuration for the RAG agent. Parameters use the format `<key>=<value>`, separated by commas. If a value contains a comma or double quote, wrap the entire parameter in double quotes.

`pg_dist_rag_conf_csv` does not choose your document source type. Sources are registered separately with `dist_rag.create_source`, using either an `s3://` URI. Include the AWS keys only when reading from S3; URL sources do not use them. Set `OPENAI_API_KEY` when using the `OPENAI` embedding provider, regardless of source type.

| Key | Applies to | Description |
| :-- | :--------- | :---------- |
| `AWS_S3_BUCKET_NAME` | S3 sources | Default S3 bucket name passed to the RAG agent as the `AWS_S3_BUCKET_NAME` environment variable. |
| `AWS_REGION` | S3 sources | AWS region for S3 API calls (for example, `us-east-1`). Passed to the agent as the `AWS_REGION` environment variable. |
| `AWS_ACCESS_KEY_ID` | S3 sources | AWS access key ID for S3 authentication. Optional if the host uses instance or profile credentials. Passed to the agent as the `AWS_ACCESS_KEY_ID` environment variable. |
| `AWS_SECRET_ACCESS_KEY` | S3 sources | AWS secret access key for S3 authentication. Passed to the agent as the `AWS_SECRET_ACCESS_KEY` environment variable. |
| `OPENAI_API_KEY` | All sources (when using `OPENAI`) | OpenAI API key for embedding generation. Passed to the agent as the `OPENAI_API_KEY` environment variable. |
| `SCRIPT_PATH` | Service setup | Path to the RAG agent startup script (`start_rag_agent.py`). Defaults to `python/ai/rag_agent/start_rag_agent.py` under the YugabyteDB root directory. |

For example, to create a single-node cluster with the RAG service and S3 document sources using [yugabyted](../../../reference/configuration/yugabyted/):

```sh
./bin/yugabyted start \
  --tserver_flags "enable_pg_dist_rag_service=true,pg_dist_rag_conf_csv=OPENAI_API_KEY=sk-proj-example,AWS_S3_BUCKET_NAME=my-bucket,AWS_REGION=us-east-1" \
  --ui false
```

For URL-based sources, only the embedding provider key is typically required at startup:

```sh
./bin/yugabyted start \
  --tserver_flags "enable_pg_dist_rag_service=true,pg_dist_rag_conf_csv=OPENAI_API_KEY=sk-proj-example" \
  --ui false
```

The RAG agent script is located at `python/ai/rag_agent/start_rag_agent.py`. A Python virtual environment with the agent dependencies must be available before the service starts.

### Enable the extension

Install pgvector first, then create the pg_dist_rag extension:

```sql
CREATE EXTENSION IF NOT EXISTS vector;
CREATE EXTENSION IF NOT EXISTS pg_dist_rag;
```

This creates the `dist_rag` schema with tables, types, functions, and views for managing sources, indexes, documents, pipelines, and the work queue.

## Build a vector index

The typical workflow has four steps: create sources, initialize a vector index, optionally add more sources, and build the index, all managed using the following four functions.

| Function | Description |
| :--- | :--- |
| dist_rag.create_source() | Register a document collection by URI (S3 bucket), with optional metadata tags for filtering. |
| dist_rag.init_vector_index() | Create a named vector index backed by a pgvector table, specifying your AI provider and embedding dimensions. |
| dist_rag.add_source_to_index() | Attach one or more sources to an index, with per-source chunking parameters. |
| dist_rag.build_index() | Enqueue the full preprocessing pipeline for all documents in the index. |

### 1. Create a source

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

### 2. Initialize a vector index

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

### 3. Add a source to an existing index

Use `add_source_to_index()` to attach additional sources to an already-created vector index, optionally with custom chunking parameters.

```sql
SELECT dist_rag.add_source_to_index(
  r_index_id := '<index_uuid>',
  r_source_id := '<source_uuid>',
  r_chunk_params := '{"chunk_size": 512, "chunk_overlap": 50, "strategy": "recursive"}'::jsonb
);
```

### 4. Build the index

Kick off preprocessing for all documents across all sources in an index. Each document gets a `PREPROCESS` task queued in the work queue. RAG agent workers claim tasks from the queue and process documents in parallel.

Provide exactly one of `r_index_id` or `r_index_name`:

```sql
SELECT dist_rag.build_index(r_index_id := '<index_uuid>');
```

```sql
SELECT dist_rag.build_index(r_index_name := 'my_knowledge_base');
```

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

After a pipeline completes, embeddings are stored in the backing table created for the vector index (for example, `public.my_knowledge_base`). Query it using standard [pgvector](../extension-pgvector/) operators:

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
  r_metadata := '{"team": "engineering", "access": "internal"}'::jsonb,
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

## Schema reference

### Tables

| Table | Description |
| :---- | :---------- |
| `dist_rag.sources` | Registered document sources. |
| `dist_rag.vector_indexes` | Vector index metadata. |
| `dist_rag.vector_index_source_mappings` | Many-to-many mapping between indexes and sources. |
| `dist_rag.documents` | Individual documents belonging to sources. |
| `dist_rag.pipeline_details` | Per-document pipeline execution records. |
| `dist_rag.work_queue` | Internal task queue with lease-based locking. |

### Enum types

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

### Views

| View | Description |
| :--- | :---------- |
| `dist_rag.vector_index_pipeline_details` | Detailed per-document pipeline execution info across all indexes. |
| `dist_rag.pipeline_stats` | Aggregated pipeline statistics per document per index. |

## Learn more

- [Build a LangGraph RAG agent](https://github.com/krishna-yb/langgraph/tree/examples/yugabytedb-rag/examples/rag) - application example that queries a pg_dist_rag index from a self-correcting LangGraph agent using the `langchain-yugabytedb` retriever to search a built index and generate answers using OpenAI
- [pgvector extension](../extension-pgvector/)
- [Develop applications with AI and YugabyteDB](../../../develop/ai/)
- [pg_dist_rag extension README](https://github.com/yugabyte/yugabyte-db/blob/master/src/postgres/yb-extensions/pg_dist_rag/README.md)
- [RAG agent source code](https://github.com/yugabyte/yugabyte-db/tree/master/python/ai/rag_agent)
