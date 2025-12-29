---
title: Building Gen-AI applications on top of YugabyteDB
headerTitle: Gen-AI applications
linkTitle: Gen-AI apps
headcontent: Build scalable and resilient AI applications with YugabyteDB
menu:
  stable:
    identifier: explore-gen-ai
    parent: explore
    weight: 110
aliases:
  - /stable/explore/going-beyond-sql/gen-ai-apps/
rightNav:
  hideH3: true
type: docs
---

Generative AI has transformed how applications interact with data. While early adoption focused on text-based chatbots, modern AI applications have evolved into multimodal systems capable of processing text, audio, and video to deliver actionable insights.

YugabyteDB provides the scalable, distributed data foundation required to run modern AI workloads, from simple chatbots to complex agentic workflows. By combining the familiarity of PostgreSQL with distributed scalability, you can store and query billions of vector embeddings without managing complex, separate infrastructure.

## Key concepts: RAG, Vectors, and MCP

### Retrieval-augmented generation

RAG is the framework used to provide large language models (LLMs) with access to your private, real-time data. Instead of relying solely on the LLM's pre-trained knowledge, a RAG-based application:

1. Retrieves relevant context from a vector database (like YugabyteDB).
1. Passes that context to the LLM along with the user's prompt.
1. Generates a response that's accurate, up-to-date, and grounded in your specific data.

### Vectors and embeddings

In AI, data is often represented as vectors (or embeddings)—long lists of numbers that capture the "meaning" of a piece of data. To make your data "understandable" to an AI:

1. Embed: Data (text, audio, or video) is converted into high-dimensional vectors (lists of numbers) using an embedding model.
1. Store: These vectors are stored in YugabyteDB using the standard pgvector extension.
1. Search: When you ask a question, the application converts the query into a vector and performs a similarity search to find the most relevant "neighbors" in your database.

Because vectors are mathematical representations of meaning, they work across all data types—allowing you to search for a video clip using a text description, or find similar songs based on audio features.

### Model Context Protocol (MCP)

MCP is an industry standard that acts as a secure bridge between AI models and your structured data. While RAG focuses on retrieving unstructured context (like documents) via vectors, MCP enables LLMs to interact directly with your structured database.

With the [YugabyteDB MCP Server](../../develop/ai/mcp-server/), you can:

1. Explore: Allow LLMs to automatically discover your database schema, table structures, and relationships.
1. Query: Ask questions in natural language. The LLM generates and executes safe, read-only SQL queries to fetch precise answers.
1. Analyze: Generate insights, visualizations, and summaries directly from your data without writing custom code.

MCP complements RAG by providing direct access to structured relational data, while RAG excels at semantic search across unstructured content. Together, they enable comprehensive AI applications that can both find similar content (via vectors) and answer precise questions about your structured data (via MCP).

## Understand AI use cases

You aren't limited to building chatbots. AI on YugabyteDB is used for a wide range of enterprise use cases:

* Summarization: Condense long documents or call transcripts into actionable summaries.
* Recommendation: Use vector similarity to suggest products, content, or services based on user behavior.
* Analysis: Detect patterns and anomalies in large datasets, such as fraud detection or sentiment analysis.
* Personalization: Tailor user experiences by matching real-time activity with historical preferences.

## YugabyteDB for AI

YugabyteDB serves as a **modern and flexible platform for AI** by providing a comprehensive foundation to build production-ready AI applications.

### Open standards, flexible foundation

YugabyteDB uses PostgreSQL's `pgvector` extension for vector storage and search, so you can work with embeddings from any model or source.

- Architected for LLM and SLM flexibility: You can choose between large language models (LLMs) or small language models (SLMs) based on your needs. Some applications require non-LLM models optimized for perception, decision-making, or control. YugabyteDB's flexible architecture supports all of these approaches.

- Build for retrieval-optimized generation (ROG): YugabyteDB enables you to build applications that find answers without expensive LLM calls, moving beyond traditional RAG to retrieval-optimized generation (ROG) that reduces costs while maintaining accuracy.

- No lock-in: YugabyteDB is 100% open source, so you can run it anywhere and leverage the massive ecosystem of PostgreSQL tools. YugabyteDB has a flexible vector indexing framework that supports the latest algorithms, including FAISS, HNSW_lib, USearch, ScANN, DiskAnn, and virtually any index.

### Open standards, flexible foundation

- Use any embedding model: Generate embeddings from OpenAI, Cohere, local models, or custom models and then store them in standard `VECTOR` columns. The database treats them as numeric vectors, so you can switch embedding models by regenerating embeddings and updating your table without any schema changes.

- Reduce LLM calls with vector search: Pre-compute and store answers, document chunks, or structured responses with their embeddings. Use vector similarity search to find relevant content. For example,

  ```sql
  SELECT * FROM documents ORDER BY embedding <=> $1 LIMIT 5;
  ```

  Return results directly for simple queries, and only call LLMs when you need generation. This pattern reduces API costs while maintaining accuracy for retrieval-based use cases.

- Flexible vector indexing: Run YugabyteDB on any infrastructure (self-hosted or cloud) with full PostgreSQL tool compatibility (for example, pg_dump, ORMs like SQLAlchemy). The vector indexing framework plugs in algorithms via backends: currently USearch (HNSW-based) and Hnswlib for ANN search. Create indexes with SQL:

  ```sql
  CREATE INDEX ON table USING hnsw (embedding vector_cosine_ops) WITH (m=16, ef_construction=64);
  ```

### Unified data sources

Effective RAG applications require more than just text; they need access to structured data, images, and logs to provide accurate context. YugabyteDB unifies data via PostgreSQL foreign data wrappers (FDWs) and vector columns, allowing queries across structured, unstructured, and external sources from one endpoint.

- Multimodal embedding storage: Store and query embeddings for diverse data types (text documents, tables and spreadsheets, images, audio transcripts, and video metadata) in a single table using pgvector. Query across modalities and join vector search results with structured tables for hybrid queries. (for example, combining market reports with historical price tables).

- Access external data sources: Use pre-bundled or installable PostgreSQL extensions like PostgreSQL Foreign Data Wrappers (FDW) to query remote databases as if they were local tables. Import data with YSQL:

  ```sql
  CREATE FOREIGN TABLE s3_data (...) SERVER s3_server OPTIONS (bucket 'my-bucket', filekey 'path/to/file.parquet');
  ```

### Simplified data preprocessing

YugabyteDB handles vector indexing automatically after your data is loaded. You still need application code for parsing, chunking, and embedding generation, but the database manages vector storage and search.

- Automatic vector index management: After you insert vectors into a table, YugabyteDB's Vector LSM automatically maintains indexes. Indexes stay in sync with table data—inserts, updates, and deletes are reflected automatically. No manual index rebuilding or maintenance required. Background compaction merges index files and removes deleted vectors.

- Your application handles preprocessing: Parse documents using libraries like Unstructured.io, PyPDF2, or custom parsers. Chunk text appropriately for your use case (sentence, paragraph, or semantic chunking). Generate embeddings using your chosen model (OpenAI, local models, and so on), and then insert into YugabyteDB.

### Elastic scale for AI needs

YugabyteDB distributes vector indexes across nodes automatically using the same sharding strategy as your tables.

- Horizontal scalability: Vector indexes are automatically distributed across the cluster. To scale storage or throughput, simply add nodes. This linear scaling supports billions of vectors without manual rebalancing.

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

- Tenant isolation: Securely isolate data for different tenants or applications within the same cluster, ensuring that multi-tenant applications maintain strict data boundaries. For example, to physically isolate via tablespaces per tenant:

  ```sql
  CREATE TABLESPACE tenant1_ts LOCATION '/path/tenant1';
  ALTER TABLE shared SET TABLESPACE tenant1_ts;
  ```

- Built-in protection: Enable encryption at rest or in motion, audit logging with pgaudit extension, and native integration with OIDC and LDAP identity providers (configure in yugabyted with `--security.oidc-config`).

- Granular control: Use PostgreSQL Role-Based Access Control (RBAC) to secure data at the tenant, table, row, and column levels. For example, you acn create a role per tenant, grant table access, and use RLS to ensure tenants only see their rows.

## Get started

You can get started with the "Hello RAG" example to build your first AI application.

{{<lead link="../../develop/ai/hello-rag/">}}
Build your first AI app with [Hello RAG](../../develop/ai/hello-rag/).
{{</lead>}}

### AI tutorials matrix

Explore the following tutorials to see how YugabyteDB integrates with different LLMs and frameworks.

| Tutorial | Use case | LLM / framework | Deployment |
| :--- | :--- | :--- | :--- |
| [Hello RAG](../../develop/ai/hello-rag/) | Build a basic Retrieval-Augmented Generation (RAG) pipeline for document-based question answering. | OpenAI | External |
| [Azure AI](../../develop/ai/azure-openai/) | Use Azure OpenAI to build a scalable RAG application with vector search. | Azure OpenAI | External |
| [Google Vertex AI](../../develop/ai/google-vertex-ai/) | Use Google Vertex AI for similarity search and generative AI workflows. | Vertex AI | External |
| [LocalAI](../../develop/ai/ai-localai/) | Build and run an LLM application entirely on-premises for privacy and security. | LocalAI | Local / on-premises |
| [Ollama](../../develop/ai/ai-ollama/) | Host and run embedding models locally for vector-based similarity search. | Ollama | Local / on-premises |
| [YugabyteDB MCP server](../../develop/ai/mcp-server/) | Enable LLMs to interact directly with YugabyteDB using natural language. | Claude / Cursor | External |
| [LlamaIndex](../../develop/ai/ai-llamaindex-openai/) | Connect LLMs to structured and unstructured data using LlamaIndex. | OpenAI / LlamaIndex | External |
| [LangChain](../../develop/ai/ai-langchain-openai/) | Build a natural language interface to query your database without writing SQL. | OpenAI / LangChain | External |

### Learn more

* [pgvector extension](../../additional-features/pg-extensions/extension-pgvector/) reference.
* [YugabyteDB AI blogs](https://www.yugabyte.com/blog/category/ai/)
* [Architecting GenAI and RAG Apps with YugabyteDB](https://www.yugabyte.com/ai/)
