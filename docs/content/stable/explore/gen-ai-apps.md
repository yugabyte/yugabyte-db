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

Generative AI has transformed how applications interact with data. While early adoption focused on text-based chatbots, modern AI applications have evolved into multi-modal systems capable of processing text, audio, and video to deliver actionable insights.

YugabyteDB provides the scalable, distributed data foundation required to run modern AI workloads, from simple chatbots to complex agentic workflows. By combining the familiarity of PostgreSQL with distributed scalability, you can store and query billions of vector embeddings without managing complex, separate infrastructure.

## Key concepts: RAG and Vectors

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

## Understand AI use cases

You aren't limited to building chatbots. AI on YugabyteDB is used for a wide range of enterprise use cases:

* Summarization: Condense long documents or call transcripts into actionable summaries.
* Recommendation: Use vector similarity to suggest products, content, or services based on user behavior.
* Analysis: Detect patterns and anomalies in large datasets, such as fraud detection or sentiment analysis.
* Personalization: Tailor user experiences by matching real-time activity with historical preferences.

## YugabyteDB for AI

YugabyteDB is uniquely positioned for AI workloads due to its distributed architecture:

* Native PostgreSQL compatibility: Support for the [pgvector extension](../../additional-features/pg-extensions/extension-pgvector/) to store and query vectors using standard SQL.
* Massive scalability: Seamlessly scale your vector search to billions of embeddings across multiple nodes and regions.
* Resilience: Ensure your AI applications stay online even during node or region failures, with zero-downtime operations.
* Hybrid search: Combine vector similarity search with standard relational filtering (for example, "Find videos similar to this clip, but only from the 'Sports' category uploaded in 2023") in a single, efficient query.

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
