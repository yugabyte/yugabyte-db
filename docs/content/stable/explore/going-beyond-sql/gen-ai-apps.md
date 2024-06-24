---
title: Building Gen-AI applications on top of YugabyteDB
headerTitle: Gen-AI applications
linkTitle: Gen-AI apps
headcontent: Build a virtual assistant for YugabyteDB
menu:
  stable:
    identifier: gen-ai-apps
    parent: going-beyond-sql
    weight: 700
rightNav:
  hideH3: true
type: docs
---

As [Generative AI (Gen-AI)](https://generativeai.net/) technologies continue to advance, their potential applications are becoming increasingly widespread. Databases have long been the backbone of structured data storage and querying for organizations, and integrating them with Gen-AI capabilities can unlock new possibilities for data-driven decision making, automation, and user experiences.

While databases are efficient for storing and querying data, it can be challenging for non-technical users to interact with them directly. This is where chatbots come into play, providing a natural language interface for users to access and manipulate data stored in databases. Because YugabyteDB is fully compatible with PostgreSQL, it can be quickly adapted to provide interaction using Retrieval Augmented Generation (RAG)-based technologies.

## Terminology

This section uses the following terms:

- _Natural Language Processing_ (NLP) enables computers to understand, interpret, and generate human language in a way that is both meaningful and beneficial.
- _Large Language Models_ (LLM) are advanced NLP models that have been trained on massive amounts of text data. These models are capable of understanding and generating human-like text with remarkable fluency and coherence.

## Retrieval-Augmented Generation

One approach to building chatbots for database interaction is the Retrieval-Augmented Generation (RAG) framework. RAG combines two powerful components: a retrieval system that can fetch relevant information from a knowledge base (in this case, the database schema and data), and a language generation model that can produce natural language responses based on the retrieved information.

The RAG approach is particularly well-suited for building chatbots that interact with YugabyteDB for several reasons.

YugabyteDB stores data in a structured format, making it easier for the retrieval component to find relevant information based on the user's query. In addition to the data itself, the database schema (tables, columns, relationships) provides a rich knowledge base for the retrieval component to understand the context and semantics of the data.

## Typical workflow of a chatbot

The primary purpose of the LLM is to convert a question in a natural language to a SQL statement. It does so as follows:

1. The database schema is sent to a LLM.
1. For better query creation, it is normal to send example questions and the respective query for fine-tuning the LLM.
1. If the LLM is internally deployed, then data from the database could also be sent to the LLM.
1. The chatbot takes in a user question or text in a natural language.
1. The chatbot then sends the text to the LLM.
1. The LLM responds with a SQL query.
1. The chatbot executes the SQL query against the database.
1. The chatbot returns the result as-is to the user or it may transform the result to a natural language using a NLP system and return that response.

{{<warning>}}Typically the data stored in the database is not sent to external systems due to privacy concerns, but some information about the data could be sent.{{</warning>}}

## Sample chatbot with YugabyteDB

Several tutorials on the different ways of setting up Gen-AI-based interfaces for your database are available:

- [Using Langchain and OpenAI](/preview/tutorials/ai/ai-langchain-openai/)
- [Using LlamaIndex and OpenAI](/preview/tutorials/ai/ai-llamaindex-openai/)
- [Using local LLMs](/preview/tutorials/ai/ai-localai/)

## Choice of LLM

There are [hundreds of LLMs](https://huggingface.co/spaces/HuggingFaceH4/open_llm_leaderboard) to choose from. The following are a few that we have tried out.

|   Type   |                                     LLM                                      |
| -------- | ---------------------------------------------------------------------------- |
| External | [GPT-4](https://openai.com/research/gpt-4) from [OpenAI](https://openai.com) |
| External | [Claude-3](https://claude.ai/) from [Anthropic](https://www.anthropic.com/)  |
| External | [Vertex AI](https://cloud.google.com/vertex-ai?hl=en) from Google            |
| Local    | [Solar](https://www.upstage.ai/solar-llm)                                    |
| Local    | [Mistral AI](https://mistral.ai/)                                            |
{.sno-1}
