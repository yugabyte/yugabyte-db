---
title: Distributed RAG
headerTitle: Distributed RAG
linkTitle: Distributed RAG
description: Build distributed RAG pipelines in YugabyteDB with pg_dist_rag, with document ingestion, chunking, and embedding generation managed from SQL.
headcontent: Manage document preprocessing and embedding pipelines from SQL using pg_dist_rag
cascade:
  tags:
    feature: tech-preview
menu:
  stable_develop:
    identifier: distributed-rag
    parent: tutorials-ai
    weight: 10
type: indexpage
showRightNav: true
---

## Overview

Retrieval-Augmented Generation (RAG) applications get a lot of attention for what happens at query time: vector similarity search, prompt assembly, LLM invocation. The harder operational problem is what happens before that; that is, the preprocessing pipeline that transforms raw documents into searchable vector embeddings.

In most implementations, that pipeline lives entirely outside the database. A Python worker fetches documents from S3 or a URL, passes them through a parsing and chunking library, calls an embedding API, and finally inserts results into pgvector. Each step has its own failure modes, its own retry semantics, and its own monitoring story. When a document fails to embed, you need to know which step it failed at and whether it should be retried. When your document corpus grows to millions of files across diverse formats, you need to know how far the pipeline has gotten and whether it's keeping up with incoming changes.

Typically this infrastructure is built from scratch. At production scale, across diverse source types and formats, this can become a significant ongoing operational burden requiring hundreds to thousands of lines of code to write, test, deploy, and keep current.

Distributed RAG manages this pipeline via an extension, pg_dist_rag, using SQL, inside the YugabyteDB database. The compute-heavy preprocessing runs in decoupled RAG workers, isolated from the database process.

The extension manages a work queue internally. Documents are processed asynchronously (parsed, chunked, and embedded using the configured provider) and results land directly in the pgvector-backed index table, queryable with standard SQL the moment each document completes.

A typical "Hello RAG" implementation requires over 100 lines of Python to handle a single directory of documents. With pg_dist_rag, the equivalent is a single function call:

```sql
SELECT dist_rag.create_source('s3://company-docs/engineering/');
```

pg_dist_rag is anchored in YugabyteDB's distributed SQL architecture. The same cluster that handles transactional workloads manages the ingestion pipeline: horizontally scalable, geo-distributed for data residency requirements, and resilient to node and availability zone failures.

## Data sources

pg_dist_rag is designed as a unified data access broker. It currently supports ingestion of PDF, text, JSON, CSV, HTML, XML, and markdown, as well as S3 source locations; S3 and OpenAI credentials are stored as RAG worker environment variables.

For data that is too large or costly to move, YugabyteDB can query it in place, eliminating the data movement cost that often makes large-scale RAG pipelines expensive and operationally complex. The result is less data to move, less storage to manage, and a reduction in the operational cost of keeping multiple systems in sync.

Parsing, chunking, and embedding are handled using a comprehensive tool stack (LangChain and Unstructured.io) giving teams flexibility at every stage of the pipeline without having to integrate those tools themselves.

## Observability

Two views surface pipeline state, providing visibility into your embedding pipeline without any external tooling:

- **dist_rag.vector_index_pipeline_details**

  Provides a per-document breakdown: which pipeline step is active, how many chunks have been processed, how many embeddings persisted, and the last error message if processing failed.

- **dist_rag.pipeline_stats**

  Provides aggregate statistics per document per index: total chunks processed, total embeddings persisted, completion rate, and cumulative execution time.

<!--
## Multi-tenancy

pg_dist_rag is multi-tenant capable. Each source carries an optional tenant_id, giving each tenant a fully isolated pipeline namespace. Teams building multi-tenant RAG applications, where one database cluster serves many customers' document collections, can use a single installation across all tenants without cross-tenant data leakage.
-->

## Core concepts

| Concept | Description |
| :------ | :---------- |
| Source | A pointer to a collection of documents (for example, an S3 bucket prefix or URL). |
| Vector index | A named index that stores embeddings for documents from one or more sources. Each index has a backing table with an HNSW vector index. |
| Document | An individual file tracked under a source and processed through the pipeline. |
| Pipeline | The workflow that chunks a document and generates embeddings for each chunk. |
| Work queue | Internal task queue (`dist_rag.work_queue`) that coordinates source creation and document preprocessing across RAG workers. |
| RAG worker | Python service that crawls sources, and parses, chunks, and generates embeddings. Runs outside the database on dedicated nodes or VMs. |
