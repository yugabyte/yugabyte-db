---
title: Artificial Intelligence with YugabyteDB
headerTitle: Develop applications with AI and YugabyteDB
linkTitle: AI
description: How to Develop Applications with AI and YugabyteDB
image:
headcontent: Add a scalable and highly-available database to your AI projects
type: indexpage
showRightNav: true
cascade:
  unversioned: true
---

YugabyteDB offers the familiarity and extensibility of PostgreSQL, while also delivering scale and resilience. Thanks to the [pgvector](../../additional-features/pg-extensions/extension-pgvector/) PostgreSQL extension, it also functions as a highly performant vector database, with enterprise scale and resilience. This means you can use YugabyteDB to support Retrieval-augmented generation (RAG) workloads, providing AI agents with knowledge of your unstructured data, while its scalability allows it to store and search billions of vectors.

Explore the following examples to get started building scalable gen AI applications that scale and never fail.

## Retrieval-augmented generation

RAG combines large language models (LLM) with external knowledge sources to produce more accurate and context-aware responses. YugabyteDB serves as the retrieval layer, storing vector representations of your data, and enabling efficient similarity searches.

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

Vector databases enable semantic search, which allows you to find content with a similar meaning even if it doesn’t include the same text.

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
