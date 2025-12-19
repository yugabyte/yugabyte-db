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

YugabyteDB is built on open standards, ensuring you are never locked into a specific model, vendor, or indexing algorithm.

- Architected for LLM and SLM flexibility: You can choose between large language models (LLMs) or small language models (SLMs) based on your needs. Some applications require non-LLM models optimized for perception, decision-making, or control. YugabyteDB's flexible architecture supports all of these approaches.

- Build for retrieval-optimized generation (ROG): YugabyteDB enables you to build applications that find answers without expensive LLM calls, moving beyond traditional RAG to retrieval-optimized generation (ROG) that reduces costs while maintaining accuracy.

- No lock-in: YugabyteDB is 100% open source, so you can run it anywhere and leverage the massive ecosystem of PostgreSQL tools. YugabyteDB has a flexible vector indexing framework that supports the latest algorithms, including FAISS, HNSW_lib, USearch, ScANN, DiskAnn, and virtually any index.

### Unified data sources

Effective RAG applications require more than just text; they need access to structured data, images, and logs to provide accurate context. YugabyteDB pulls in disparate data sources to optimize RAG architectures, enabling your applications to access different types of data from a single platform.

- Enhanced contextual understanding (multimodal): Store and query embeddings for text documents, tables and spreadsheets, images, audio transcripts, and video metadata in a single system. This cross-referencing reduces hallucinations and enables complex queries (for example, combining market reports with historical price tables).

- Simplified data access: YugabyteDB supports many different data sources through rich PostgreSQL extensions that allow easy importing or stay-in-place access to data on S3, Iceberg, Parquet formats, logs, Salesforce, and more. This unified approach saves time, simplifies access control, and reduces costs.

### Simplified data preprocessing

YugabyteDB offers a turnkey system to manage the preprocessing pipeline—parsing, chunking, and generating embeddings.

- Automated pipelines: Eliminate the need to write custom application code to glue together tools like LangChain or Unstructured.io. YugabyteDB orchestrates the entire flow from ingestion to vector storage, ensuring your vector indexes stay in sync with your source data automatically.

- Comprehensive tooling: YugabyteDB supports standardizing the tool stack to handle complex tasks (parsing, chunking, and embedding). This means you don't need to spend time writing custom application code and integrating multiple separate ecosystem tools, which is often complex and error-prone.

### Elastic scale for AI needs

YugabyteDB is built for the elastic scale that AI applications require, ensuring performance does not degrade as your data grows and helping you manage costs across the AI pipeline.

- Infinite horizontal scalability: Vector indexes are automatically distributed across the cluster. To scale storage or throughput, simply add nodes. This linear scaling supports billions of vectors without manual rebalancing.

- High performance: Low-latency distributed architecture ensures fast inference even as your dataset grows to billions of vectors.

- Zero downtime: Perform upgrades, scale-outs, and maintenance without taking your AI application offline.

- Manage costs: Deploy anywhere—on-premises, hybrid, or multi-cloud—to take advantage of the best infrastructure unit economics. You can easily swap LLMs or cloud providers to optimize ROI without changing your database layer.

### Secure by design

YugabyteDB provides enterprise-grade security at every layer for AI applications, including tenant isolation, data and LLM compliance, and protection against AI-specific threats.

- Data sovereignty and LLM compliance: With Row-Level Geo-Partitioning, you can pin specific user data to specific geographic regions to comply with General Data Protection Regulation (GDPR) and data residency laws.

- Tenant isolation: Securely isolate data for different tenants or applications within the same cluster, ensuring that multi-tenant applications maintain strict data boundaries.

- Built-in protection: Features include encryption at rest and in motion, audit logging, and native integration with OIDC and LDAP identity providers.

- Granular control: Leverage standard PostgreSQL Role-Based Access Control (RBAC) to secure data at the tenant, table, row, and column levels.

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
