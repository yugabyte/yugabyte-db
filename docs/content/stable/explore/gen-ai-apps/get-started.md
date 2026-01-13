---
title: Get started with Gen-AI applications
headerTitle: Get started
linkTitle: Get started
headcontent: Build your first AI application with YugabyteDB
menu:
  stable:
    identifier: explore-gen-ai-get-started
    parent: explore-gen-ai
    weight: 10
type: docs
---

Get started with AI and YugabyteDB using the "Hello RAG" example to build your first AI application.

The Hello RAG tutorial walks you through building a complete Retrieval-Augmented Generation pipeline using YugabyteDB, that powers many enterprise AI applications, from customer support chatbots to semantic search systems.

In this tutorial, you will:

1. Vectorize data: Split local documents into chunks and generate embeddings using OpenAI.
1. Store and index: Insert those embeddings into YugabyteDB using the pgvector extension.
1. Retrieve and generate: Perform a vector similarity search to find relevant context and use an LLM (like GPT-4) to generate a grounded, accurate response.

{{<lead link="../../../develop/ai/hello-rag/">}}
Build your first AI app with [Hello RAG](../../../develop/ai/hello-rag/).
{{</lead>}}

## AI tutorials

Explore the following tutorials to see how YugabyteDB integrates with different LLMs and frameworks.

{{<tip title="Tips">}}

- Use YugabyteDB {{<release "2025.1">}} or later to get the latest vector indexing capabilities and MCP features.

- No cluster? No problem. Run the latest YugabyteDB version locally on a macOS (using Docker or the yugabyted binary) or any Linux VM to try these tutorials.

{{</tip>}}

| Tutorial | Use case | LLM / framework | LLM location |
| :--- | :--- | :--- | :--- |
| [Hello RAG](../../../develop/ai/hello-rag/) | Build a basic Retrieval-Augmented Generation (RAG) pipeline for document-based question answering. | OpenAI | External |
| [Azure AI](../../../develop/ai/azure-openai/) | Use Azure OpenAI to build a scalable RAG application with vector search. | Azure OpenAI | External |
| [Google Vertex AI](../../../develop/ai/google-vertex-ai/) | Use Google Vertex AI for similarity search and generative AI workflows. | Vertex AI | External |
| [LocalAI](../../../develop/ai/ai-localai/) | Build and run an LLM application entirely on-premises for privacy and security. | LocalAI | Local / on-premises |
| [Ollama](../../../develop/ai/ai-ollama/) | Host and run embedding models locally for vector-based similarity search. | Ollama | Local / on-premises |
| [YugabyteDB MCP server](../../../develop/ai/mcp-server/) | Enable LLMs to interact directly with YugabyteDB using natural language. | Claude / Cursor | External |
| [LlamaIndex](../../../develop/ai/ai-llamaindex-openai/) | Connect LLMs to structured and unstructured data using LlamaIndex. | OpenAI / LlamaIndex | External |
| [LangChain](../../../develop/ai/ai-langchain-openai/) | Build a natural language interface to query your database without writing SQL. | OpenAI / LangChain | External |

## Learn more

- [pgvector extension](../../../additional-features/pg-extensions/extension-pgvector/) reference.
- [YugabyteDB AI blogs](https://www.yugabyte.com/blog/category/ai/)
- [Architecting GenAI and RAG Apps with YugabyteDB](https://www.yugabyte.com/ai/)
