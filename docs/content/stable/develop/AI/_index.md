---
title: Artificial Intelligence with YugabyteDB
headerTitle: Develop applications with AI and YugabyteDB
linkTitle: AI
description: How to Develop Applications with AI and YugabyteDB
image:
headcontent: Build AI applications with YugabyteDB - support RAG, semantic search, and AI agents at enterprise scale
type: indexpage
showRightNav: true
cascade:
  unversioned: true
---

YugabyteDB offers the familiarity and extensibility of PostgreSQL, while also delivering scale and resilience. Its distributed nature combines enterprise-grade vector search with ACID transactions. YugabyteDB enables you to store embeddings alongside transactional data, perform vector similarity searches with full SQL capabilities, and scale to billions of vectors across multiple regions, all with PostgreSQL compatibility and zero-downtime operations.

Using the [pgvector](../../additional-features/pg-extensions/extension-pgvector/) PostgreSQL extension, YugabyteDB functions as a highly performant vector database, with enterprise scale and resilience. This means you can use YugabyteDB to support Retrieval-augmented generation (RAG) workloads, providing AI agents with knowledge of your unstructured data, while its scalability allows it to store and search billions of vectors.

Learn more about developing GenAI and RAG applications with YugabyteDB:

- [Introducing New YugabyteDB Functionality for Ultra-Resilient AI Apps](https://www.yugabyte.com/blog/new-yugabytedb-functionality-for-ultra-resilient-ai-apps/)
- [Introducing the YugabyteDB MCP Server](https://www.yugabyte.com/blog/yugabytedb-mcp-server/)
- [How to Build a RAG Workflow for Agentic AI without Code](https://www.yugabyte.com/blog/build-a-rag-workflow-for-agentic-ai-without-codev/)
- [From RAG to Riches: AI That Knows Your Support Stack](https://www.yugabyte.com/blog/rag-ai-that-knows-your-support-stack/)

Explore the following examples to get started building scalable gen AI applications with YugabyteDB.

## Retrieval-augmented generation

Build a Retrieval-Augmented Generation pipeline with YugabyteDB.

{{<index/block>}}
{{<index/item
    title="Hello RAG"
    body="Build a Retrieval-Augmented Generation (RAG) pipeline with YugabyteDB."
    href="hello-rag/"
    icon="fa-thin fa-vector-circle">}}
{{</index/block>}}

{{<index/block>}}

{{<index/item
    title="Similarity Search using Azure AI"
    body="Build a scalable generative AI application using YugabyteDB as the database backend."
    href="azure-openai/"
    icon="/images/tutorials/azure/icons/OpenAI-Icon.svg">}}

{{<index/item
    title="Similarity Search using Google Vertex AI"
    body="Deploy generative AI applications using Google Vertex AI and YugabyteDB."
    href="google-vertex-ai/"
    icon="/images/tutorials/google/icons/Google-Vertex-AI-Icon.svg">}}

{{</index/block>}}

## Vector basics

Use YugabyteDB as the database backend for LLM applications.

{{<index/block>}}
{{<index/item
    title="Similarity search using LocalAI"
    body="Build an LLM application, hosted locally or on-prem using LocalAI and YugabyteDB."
    href="ai-localai/"
    icon="/images/tutorials/ai/icons/localai-icon.svg">}}

{{<index/item
    title="Similarity search using Ollama"
    body="Build an application with a locally-hosted embedding model using Ollama and YugabyteDB."
    href="ai-ollama/"
    icon="/images/tutorials/ai/icons/ollama-icon.svg">}}
{{</index/block>}}

## Agentic, multiple data sources, and multi-step reasoning

Learn how you can use YugabyteDB as the foundation for your next AI agent application.

{{<index/block>}}

{{<index/item
    title="YugabyteDB MCP Server"
    body="Get LLMs to interact directly with YugabyteDB."
    href="mcp-server/"
    icon="fa-thin fa-comment">}}

{{<index/item
    title="Use a knowledge base using Llama-Index"
    body="Build a scalable RAG (Retrieval-Augmented Generation) app using LlamaIndex and OpenAI."
    href="ai-llamaindex-openai/"
    icon="/images/tutorials/ai/icons/llamaindex-icon.svg">}}

{{<index/item
    title="Query without SQL using LangChain"
    body="Build scalable applications with LLM integrations using LangChain and OpenAI."
    href="ai-langchain-openai/"
    icon="/images/tutorials/ai/icons/langchain-icon.svg">}}
{{</index/block>}}
