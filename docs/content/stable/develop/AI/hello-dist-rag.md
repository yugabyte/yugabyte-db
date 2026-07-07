---
title: Hello Distributed RAG
headerTitle: Hello Distributed RAG
linkTitle: Hello Distributed RAG
description: Build a RAG pipeline in SQL using the pg_dist_rag extension
headcontent: Build a RAG ingestion pipeline with three SQL calls
tags:
  feature: tech-preview
menu:
  stable_develop:
    identifier: tutorials-rag-hello-dist
    parent: tutorials-ai-rag
    weight: 41
type: docs
---

{{<tags/feature/tp idea="2537">}}In the [Hello RAG](../hello-rag/) tutorial, the application does all the ingestion work itself: it loads documents, splits them into chunks, calls the OpenAI embeddings API, and inserts each vector into a table you created by hand. That is roughly 100 lines of Python and a llama-index dependency before you can ask your first question.

This tutorial builds the same question-answering application using the [pg_dist_rag](../../../additional-features/pg-extensions/extension-pg-dist-rag/) extension, which turns document ingestion into a database operation. You register an S3 bucket as a document source, create a vector index, and build it — three SQL calls. RAG workers that ship with YugabyteDB crawl the bucket, chunk and embed each document, and write the vectors into a table that pg_dist_rag creates for you. Your application keeps only the part that is actually yours: asking questions.

| Ingestion step | Hello RAG | Hello Distributed RAG |
| :--- | :--- | :--- |
| Load documents | Python (`SimpleDirectoryReader`) | `dist_rag.create_source()` |
| Create table and vector index | Manual `CREATE TABLE` and `CREATE INDEX` | `dist_rag.init_vector_index()` |
| Chunk text | llama-index in the application | RAG worker |
| Generate embeddings | OpenAI calls in the application | RAG worker |
| Insert vectors | Row-by-row inserts in Python | RAG worker, in durable batches |
| Monitor progress | Print statements | SQL monitoring views |
| Ask questions | `question.py` | `ask.py` (same approach) |

Because the pipeline state lives in the database, you can watch ingestion progress with plain SQL, point the same setup at your own S3 buckets, and scale ingestion by starting more workers — without changing the application.

## Prerequisites

- Python 3.11 (required by the RAG worker) and Python 3 for the application.
- YugabyteDB {{<release "2026.1">}} or later with the pg_dist_rag extension.
- An [OpenAI API key](https://platform.openai.com/api-keys), exported as `OPENAI_API_KEY`.
- AWS credentials for the RAG worker. The worker uses them to list the source bucket; any valid AWS credentials work for the public sample bucket, because it grants public read access.

## Set up YugabyteDB

1. [Download and install](https://download.yugabyte.com) YugabyteDB {{<release "2026.1">}} or later.

1. Start a single-node cluster using [yugabyted](../../../reference/configuration/yugabyted/).

    ```sh
    ./bin/yugabyted start --advertise_address=127.0.0.1
    ```

1. Connect to the cluster using [ysqlsh](../../../api/ysqlsh/) and enable the extensions:

    ```sh
    ./bin/ysqlsh -U yugabyte
    ```

    ```sql
    CREATE EXTENSION IF NOT EXISTS vector;
    CREATE EXTENSION IF NOT EXISTS pg_dist_rag;
    ```

Note what you don't do here: no `CREATE TABLE`, and no `CREATE INDEX`. pg_dist_rag creates the vector table and its ybhnsw index when the application initializes the vector index.

## Start a RAG worker

RAG workers do the heavy lifting — crawling sources, chunking documents, and generating embeddings — outside your database and your application. A worker is a standalone Python service that polls the `dist_rag.work_queue` table for tasks; it can run on any machine that can reach the database, S3, and the OpenAI API.

1. In your YugabyteDB installation directory, set up the worker environment:

    ```sh
    cd python/ai/rag_agent
    python3.11 -m venv .venv
    source .venv/bin/activate
    pip install -r requirements.txt
    ```

1. Configure the worker:

    ```sh
    export YUGABYTEDB_CONNECTION_STRING="postgresql://yugabyte:yugabyte@127.0.0.1:5433/yugabyte"
    export OPENAI_API_KEY='your OpenAI key'
    export AWS_REGION=us-east-1
    export AWS_ACCESS_KEY_ID='your AWS key ID'
    export AWS_SECRET_ACCESS_KEY='your AWS secret key'
    ```

    The AWS credentials are used to list the objects in the source bucket. Downloading the documents themselves falls back to public HTTPS URLs when the bucket is public.

1. Start the worker:

    ```sh
    python __main__.py
    ```

    Leave it running. The worker waits until the pg_dist_rag extension exists in the database, then polls the work queue for tasks.

By default a worker processes text-family documents (plain text, Markdown, JSON, CSV, XML, HTML). To also process PDFs, start another worker with `WORKER_DOCUMENT_TYPE=PDF`. You can start as many workers as you like — each claims different documents from the queue, so ingestion scales horizontally and independently of your database nodes.

## Set up the application

Download the application and install its dependencies in a virtual environment:

```sh
git clone https://github.com/YugabyteDB-Samples/hello-dist-rag.git
cd hello-dist-rag
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
export OPENAI_API_KEY='your OpenAI key'
```

The requirements are just `openai` and `psycopg2` — there is no document-loading or embedding library, because the application no longer does that work.

By default, the application ingests a public sample bucket containing the same Paul Graham essay used in Hello RAG. To index your own documents instead, upload them to an S3 prefix and set `SOURCE_URI`:

```sh
export SOURCE_URI='s3://your-bucket/your-docs/'
```

## Run the application

In the `hello-dist-rag` directory, do the following:

1. Build the knowledge base:

    ```sh
    python load.py
    ```

    ```output
    ✅ Successfully connected to the database.

    🧩 Extensions ready: vector, pg_dist_rag.

    📄 Registered source s3://yugabytedb-sample-data/hello-dist-rag/
       source_id: 1c9a5d3e-7c41-4a5e-9f0d-2b8a4c6e1f37

    🗂️  Created vector index 'hello_dist_rag' (text-embedding-3-small, 1536 dimensions)
       index_id: 7f2b9c1a-53d6-4e8b-a1c9-0d4e6f8a2b5c

    🔍 Waiting for a RAG worker to crawl the source...
    📦 Crawl complete. 1 document(s) registered:
       - hello-dist-rag/paul_graham_essay.txt

    🚀 Index build queued for 'hello_dist_rag'.

    ⚙️  Workers are chunking and embedding documents...
       📥 hello-dist-rag/paul_graham_essay.txt: QUEUED | chunks: 0 | embeddings: 0
       📥 hello-dist-rag/paul_graham_essay.txt: PROCESSING | chunks: 100 | embeddings: 100
       📥 hello-dist-rag/paul_graham_essay.txt: COMPLETED | chunks: 142 | embeddings: 142

    🎉 Knowledge base ready: 142 chunks embedded in public.hello_dist_rag.
       Ask it something:  python ask.py
    ```

    While the build runs (or any time after), you can watch the same progress from ysqlsh:

    ```sql
    SELECT document_name, document_status, chunks_processed,
           embeddings_persisted, last_error_message
    FROM dist_rag.vector_index_pipeline_details
    WHERE index_name = 'hello_dist_rag';
    ```

1. Query the LLM with context retrieved from the knowledge base:

    ```sh
    python ask.py
    ```

    ```output
    Ask me a question (press Ctrl+C to quit):

    ❓ Your question: tell me about paul graham

    🔍 Retrieved context snippets:
    - 'Over the next several years Paul Graham' (distance: 0.1471)
    - 'The article is about Paul Graham\n\nWhat ' (distance: 0.1513)
    - 'Paul Graham certainly did. So at the end' (distance: 0.1523)
    - 'They either lived long ago or were myste' (distance: 0.1530)
    - 'But the most important thing Paul Graha' (distance: 0.1583)
    - 'You can do something similar on a map of' (distance: 0.1621)
    - 'When Paul Graham was dealing with some ' (distance: 0.1628)

    💡 Answer:
    Paul Graham is a programmer, writer, and investor. Before college he focused on
    writing and programming, later studying AI and then art. He co-founded Viaweb,
    which was sold to Yahoo, wrote the essays collected in "Hackers & Painters",
    created the Lisp dialect Arc, and co-founded Y Combinator with Jessica
    Livingston, which he ran until handing it over to Sam Altman in 2013.
    ```

## Review the application

The application has two parts: `load.py` builds the knowledge base through pg_dist_rag, and `ask.py` answers questions using the same retrieve-then-generate approach as Hello RAG.

### load.py: three SQL calls

Register the S3 bucket as a document source. This queues a crawl task that a worker picks up:

```python
cursor.execute(
    "SELECT dist_rag.create_source(r_source_uri := %s);", (source_uri,)
)
source_id = cursor.fetchone()[0]
```

Create the vector index. This creates the backing table `public.hello_dist_rag` with a `vector(1536)` column and a ybhnsw index on it — the DDL you wrote by hand in Hello RAG:

```python
cursor.execute(
    """
    SELECT dist_rag.init_vector_index(
        r_index_name := %s,
        r_sources := ARRAY[%s]::UUID[],
        r_ai_provider := 'OPENAI',
        r_embedding_model_params := %s::jsonb
    );
    """,
    (index_name, source_id, '{"model": "text-embedding-3-small", "dimensions": 1536}'),
)
```

After the crawl completes, queue every discovered document for chunking and embedding:

```python
cursor.execute(
    "SELECT dist_rag.build_index(r_index_name := %s);", (index_name,)
)
```

Everything else in `load.py` is progress reporting: it polls `dist_rag.sources` for the crawl status and the `dist_rag.vector_index_pipeline_details` view for per-document chunk and embedding counts, which the workers update in batches as they go.

### ask.py: unchanged concept

`ask.py` works like Hello RAG's `question.py`. It embeds the question with the same model the index was built with, retrieves the closest chunks, and passes them to a chat model:

```python
response = client.embeddings.create(
    model="text-embedding-3-small", input=question, dimensions=1536
)
```

```sql
SELECT chunk_text, embeddings <=> %s::vector AS distance
FROM public.hello_dist_rag
ORDER BY embeddings <=> %s::vector
LIMIT %s;
```

The only differences from Hello RAG are the table and column names (`public.hello_dist_rag` with `chunk_text` and `embeddings`, created by pg_dist_rag) and that the question is embedded with the OpenAI client directly instead of through llama-index.

### What is not in this application

- No document readers or directory walking — the worker crawls S3.
- No chunking code or text splitters — the worker chunks documents.
- No embedding calls during ingestion — the worker generates and batch-inserts embeddings.
- No `CREATE TABLE` or `CREATE INDEX` — `init_vector_index()` creates both.
- No llama-index dependency — `requirements.txt` is `openai` and `psycopg2`.

To ingest real content — internal docs, knowledge bases, support archives — point `SOURCE_URI` at your own S3 prefix and rerun `load.py`. The application does not change.

## Read more

- [pg_dist_rag extension](../../../additional-features/pg-extensions/extension-pg-dist-rag/)
- [Hello RAG](../hello-rag/) — the same pipeline built by hand
- [pgvector extension](../../../additional-features/pg-extensions/extension-pgvector/)
- [Architecting GenAI and RAG Apps with YugabyteDB](https://www.yugabyte.com/ai/)
