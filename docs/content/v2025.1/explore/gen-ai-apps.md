---
title: Building Gen-AI applications on top of YugabyteDB
headerTitle: Gen-AI applications
linkTitle: Gen-AI apps
headcontent: Build scalable and resilient AI applications with YugabyteDB
description: Learn how YugabyteDB provides a modern and flexible platform for AI
menu:
  v2025.1:
    identifier: explore-gen-ai
    parent: explore
    weight: 205
rightNav:
  hideH4: true
type: docs
---

Generative AI has transformed how applications interact with data. While early adoption focused on text-based chatbots, modern AI applications have evolved into multimodal systems capable of processing text, audio, and video to deliver actionable insights.

YugabyteDB provides the scalable, distributed data foundation required to run modern AI workloads, from simple chatbots to complex agentic workflows. By combining the familiarity of PostgreSQL with distributed scalability, you can store and query billions of vector embeddings without managing complex, separate infrastructure.

{{<lead link="(https://www.yugabyte.com/blog/benchmarking-1-billion-vectors-in-yugabytedb/)">}}
Learn how to [power AI at scale using YugabyteDB](https://www.yugabyte.com/blog/benchmarking-1-billion-vectors-in-yugabytedb/).
{{</lead>}}

## Key concepts

### Retrieval-augmented generation

Retrieval-augmented generation (RAG) is the framework used to provide large language models (LLMs) with access to your private, real-time data. Instead of relying solely on the LLM's pre-trained knowledge, a RAG-based application:

1. Retrieves relevant context from a vector database (like YugabyteDB).
1. Passes that context to the LLM along with the user's prompt.
1. Generates a response that's accurate, up-to-date, and grounded in your specific data.

### Vectors and embeddings

In AI, data is often represented as vectors (or embeddings). These are long lists of numbers that capture the semantic meaning of a piece of data. To make your data understandable to an AI:

1. Embed: Data (text, audio, or video) is converted into high-dimensional vectors (lists of numbers) using an embedding model.
1. Store: These vectors are stored in YugabyteDB using the familiar pgvector extension API.
1. Search: When you ask a question, the application converts the query into a vector and performs a similarity search to find the most relevant neighbors in your database.

Vector-based similarity searches are commonly used in step 1 of the [RAG](#retrieval-augmented-generation) workflow. Specifically, they are used to generate an abbreviated context (consisting of only a handful of data excerpts). The alternative, using *all* data as context, is typically avoided because it's inefficient, costly, and often infeasible.

### Model Context Protocol (MCP)

MCP is an industry standard that acts as a secure bridge between AI applications and your data sources. Whereas RAG enhances the _input_ to an LLM by adding context to the prompt, MCP enhances an LLM's runtime capabilities. MCP allows an LLM, during the course of generating a response, to actively access enterprise services like YugabyteDB to fetch the specific data it needs.

In short, MCP transforms YugabyteDB from a static data source into a dynamic tool that an LLM can use to generate accurate, real-time, data-driven responses.

Using the [YugabyteDB MCP Server](/stable/develop/ai/mcp-server/), you can:

1. Explore: Enable LLMs to automatically discover your database schema, table structures, and relationships.
1. Query: Ask questions in natural language. The LLM generates and executes safe, read-only SQL queries to fetch precise answers.
1. Analyze: Generate insights, visualizations, and summaries directly from your data without writing custom code.

#### MCP and RAG: Better together

MCP complements RAG by providing direct access to structured relational data, while RAG excels at semantic search across unstructured content. Together, they enable comprehensive AI applications that can both find similar content (via vectors) and answer precise questions about your structured data (via MCP).

## AI use cases

You aren't limited to building chatbots. AI on YugabyteDB is used for a wide range of enterprise use cases:

- Summarization: Condense long documents or call transcripts into actionable summaries.
- Recommendation: Use vector similarity to suggest products, content, or services based on user behavior.
- Analysis: Detect patterns and anomalies in large datasets, such as fraud detection or sentiment analysis.
- Personalization: Tailor user experiences by matching real-time activity with historical preferences.

## YugabyteDB for AI

YugabyteDB serves as a **modern and flexible platform for AI** by providing a comprehensive foundation to build production-ready AI applications.

### Open standards, flexible foundation

YugabyteDB combines the PostgreSQL pgvector extension APIs with [Vector LSM](https://www.yugabyte.com/blog/yugabytedb-vector-indexing-architecture/), a scalable, distributed, high throughput vector store, so you can work with embeddings from any model or source.

- Architected for LLM and SLM flexibility: You can choose between LLMs or small language models (SLMs) based on your needs. Some applications require non-LLM models optimized for perception, decision-making, or control. YugabyteDB's flexible architecture supports all of these approaches.

- Build for retrieval-optimized generation (ROG): YugabyteDB enables you to build applications that find answers without expensive LLM calls, moving beyond traditional RAG to retrieval-optimized generation (ROG) that reduces costs while maintaining accuracy.

- No lock-in: YugabyteDB is 100% open source, so you can run it anywhere and leverage the massive ecosystem of PostgreSQL tools. YugabyteDB has a flexible vector indexing framework that supports the latest algorithms, including FAISS, HNSW_lib, USearch, ScANN, DiskAnn, and virtually any index.

- Use any embedding model: Generate embeddings from OpenAI, Cohere, local models, or custom models, and then store them in standard `VECTOR` columns. The database treats them as numeric vectors, so you can switch embedding models by regenerating embeddings and updating your table without any schema changes.

- Flexible vector indexing: Run YugabyteDB on any infrastructure (self-hosted or cloud) with full PostgreSQL tool compatibility (for example, pg_dump, ORMs like SQLAlchemy). Internally, YugabyteDB uses a pluggable and swappable vector indexing framework. While it currently leverages USearch for high-performance vector search, the architecture is designed to be "algorithm-agnostic." This allows other leading libraries (such as hnswlib or FAISS) to be seamlessly integrated as the AI landscape evolves.

AI applications require access to massive volumes of unstructured and diverse data (text, audio, video, and images) stored across fragmented locations like cloud buckets, local disks, and external applications. So it's a significant challenge to either get this diverse source data into YugabyteDB, or allow YugabyteDB to access this data in its original location.

YugabyteDB unifies data access by leveraging the PostgreSQL ecosystem:

- Native data access: Using built-in PostgreSQL capabilities and extensions, YugabyteDB can access (and optionally import) data in its native format, ranging from unstructured files (PDF, DOCX, MPEG) to structured formats (CSV, Apache Iceberg), directly from local storage or cloud buckets (such as S3 or GCS).
- Foreign Data Wrappers (FDW): YugabyteDB allows access to other databases via PostgreSQL FDWs, which you can use to query remote databases as if they were local tables. For example, you can query an S3 Bucket via FDW:

  ```sql
  CREATE FOREIGN TABLE s3_data (...)
  SERVER s3_server
  OPTIONS (bucket 'my-bucket', filekey 'path/to/file.parquet');
  ```

### Simplified data preprocessing

Before unstructured data can be used for vector searches in YugabyteDB, it typically needs to be preprocessed. This traditionally involves a multi-stage pipeline:

1. Parsing: Extracting usable content from raw files (PDF, Word, etc.).
1. Chunking: Breaking data into semantically modular units (sentences, paragraphs, or sections).
1. Embedding: Generating and storing vector representations of those chunks.

Building and maintaining this high-scale pipelined system often creates a significant operational burden for application teams.

#### Built-in preprocessing

YugabyteDB simplifies this by offering optional, turnkey tooling built directly into the YugabyteDB database cluster:

- Automated preprocessing: YugabyteDB parses documents using integrated libraries (like Unstructured.io, PyPDF2, custom parsers), chunks the text appropriately for your use case, and generates embeddings using your chosen model (OpenAI, local models, and so on) before inserting them into your tables.
- Automatic vector index management: After you insert vectors into a table, YugabyteDB's Vector LSM automatically maintains and synchronizes indexes. Indexes stay in sync with table data - inserts, updates, and deletes are reflected in real-time, and background compaction merges index files without requiring manual rebuilding.

This capability is currently in [Tech Preview](/stable/releases/versioning/#feature-maturity). Contact {{% support-general %}} for more information.

### Elastic scale for AI needs

YugabyteDB distributes vector indexes across nodes automatically using the same sharding strategy as your tables.

- Horizontal scalability: Vector indexes are automatically distributed across the cluster. To scale storage or throughput, just add nodes. This linear scaling supports billions of vectors without manual rebalancing.

- High performance: Low-latency distributed architecture ensures fast inference even as your dataset grows to billions of vectors.

- Zero downtime: Perform upgrades, scale-outs, and maintenance without taking your AI application offline. Online scaling lets you add or remove nodes without stopping the database. Vector indexes rebuild automatically during tablet splitting and rebalancing.

- Manage costs: Deploy on-premises, in a single cloud, or across multiple clouds. You can move or replace model inference infrastructure (where AI models are executed) without changing your YugabyteDB schemas, queries, or retrieval logic.

### Secure by design

YugabyteDB secures AI apps with PostgreSQL RBAC, encryption, and distributed features like geo-partitioning.

- Data sovereignty and LLM compliance: Use Row-Level Geo-Partitioning to pin specific user data to specific geographic regions to comply with General Data Protection Regulation (GDPR) and data residency laws. For example,

  ```sql
  ALTER TABLE users ADD PARTITION BY LIST (region);
  CREATE TABLESPACE eu_ts LOCATION '/path/eu';
  ALTER TABLE users PARTITION eu SET TABLESPACE eu_ts;
  ```

- Built-in protection: Enable encryption at rest or in motion, audit logging using the pgaudit extension, and authentication with OIDC and LDAP identity providers (configure in yugabyted with `--security.oidc-config`).

- Granular control: Use PostgreSQL Role-Based Access Control (RBAC) to secure data at the tenant, table, row, and column levels. For example, you can create a role per tenant, grant table access, and use RLS to ensure tenants only see their rows.

## Get started

Ready to build your first AI application? Get started with tutorials, examples, and guides.

{{<lead link="/">}}
[Get started with Gen-AI applications](/stable/develop/AI/#get-started).
{{</lead>}}
