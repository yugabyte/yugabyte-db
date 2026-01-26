---
title: Hello RAG
headerTitle: Hello RAG
linkTitle: Hello RAG
description: Build a Retrieval-Augmented Generation (RAG) pipeline with YugabyteDB
headcontent: Build a Retrieval-Augmented Generation pipeline with YugabyteDB
menu:
  stable_develop:
    identifier: tutorials-rag-hello
    parent: tutorials-ai-rag
    weight: 40
type: docs
---

This tutorial guides you through constructing a Retrieval-Augmented Generation (RAG) pipeline using YugabyteDB, a distributed SQL database. By integrating YugabyteDB with vector search capabilities, you can enhance your AI applications with scalable, resilient, and low-latency access to semantically rich data.

## Why use RAG?

RAG combines large language models (LLM) with external knowledge sources to produce more accurate and context-aware responses. In this setup, YugabyteDB serves as the retrieval layer, storing vector representations of your data, and enabling efficient similarity searches.

One of the more compelling AI applications is frontline customer support.

However, support content typically resides across many spaces, including public documentation, internal knowledge bases, Slack threads, support tickets, and more. To leverage this content effectively, you need to vectorize it. This means converting it into embeddings that preserve semantic meaning and enable efficient searching. Many companies can't store this data externally. Hosting your own vector database ensures control, privacy, and security—key requirements for enterprise adoption.

To have support documents on premises and supply the LLM with needed context from the documents, you do the following:

1. Transform all the support content into vector embeddings and store them in a PostgreSQL-compatible vector database like YugabyteDB, which supports hybrid transactional and vector workloads. Embeddings are stored in YugabyteDB using the [pgvector extension](../../../additional-features/pg-extensions/extension-pgvector/).
1. When a user asks a question, generate an embedding for that question, search the internal vector store for semantically similar content, and pass those results as context to the LLM to generate accurate, relevant responses.

The following basic example does the following:

1. Loads a directory of files (`hello_rag/data/`).
1. Splits them into chunks.
1. Converts the chunks into vectors using OpenAI embeddings.
1. Stores each chunk's ID, text, and vector in a YugabyteDB table called `vectors`.

The same setup can be used for real support content, including internal docs, chat logs, email threads, and more.

All you need is an OpenAI API key (exported as OPENAI_API_KEY), and a running YugabyteDB instance with vector support enabled.

Once loaded, you can query the vector table with any user question, retrieve the most relevant matches, and use them to feed a large language model like GPT-4.

This same approach isn't limited to just "Ask Your Support Knowledge Base" scenarios. It can also be applied to a wide range of enterprise use cases, including:

- Semantic search
- Recommendations (for products, services, advice, and more)
- Personalization
- Fraud detection

## Prerequisites

- Python 3
- YugabyteDB {{<release "2025.1">}} or later
- An [OpenAI API key](https://platform.openai.com/api-keys).

## Set up the application

Download the application and provide settings specific to your deployment:

1. Install the application dependencies in virtual environment.

    ```sh
    python3 -m venv aiblog
    source aiblog/bin/activate
    cd aiblog
    pip install llama-index
    pip install psycopg2
    export OPENAI_API_KEY='your openAI key'
    ```

    {{< tip title="Installing psycopg2" >}}

If you have issues with `pip install psycopg2`, check the [installation instructions](/stable/develop/drivers-orms/python/postgres-psycopg2-reference/#fundamentals).

    {{< /tip >}}

1. Clone the repository.

    ```sh
    git clone https://github.com/YugabyteDB-Samples/hello_rag.git
    ```

    The `hello_rag/data` directory includes a file about "paul_graham". You can place any text data in this directory to supplement the LLM retrieval.

## Set up YugabyteDB

1. [Download and install](https://download.yugabyte.com) YugabyteDB {{<release "2025.1">}} or later.

1. Start a single-node cluster using [yugabyted](../../../reference/configuration/yugabyted/).

    ```sh
    ./bin/yugabyted start --advertise_address=127.0.0.1
    ```

1. Connect to the cluster using [ysqlsh](../../../api/ysqlsh/).

    ```sh
    ./bin/ysqlsh -U yugabyte
    ```

1. Enable the pgvector extension:

    ```sql
    CREATE EXTENSION vector;
    ```

1. Create a table for the vector embeddings:

    ```sql
    CREATE TABLE vectors (
        id           TEXT PRIMARY KEY,
        article_text TEXT,
        embedding    VECTOR(1536)
    );

    CREATE INDEX NONCONCURRENTLY ON vectors USING ybhnsw (embedding vector_cosine_ops);
    ```

## Run the application

In the `hello_rag` directory, do the following:

1. Edit `insert.py` and `question.py` to set `connection_string` for your YugabyteDB database.

    For example:

    ```python
    connection_string = "postgresql://yugabyte:yugabyte@127.0.0.1:5433/yugabyte"
    ```

1. Insert embeddings in YugabyteDB from `./data`:

    ```python
    python insert.py
    ```

    ```output
    ✅ Successfully connected to the database.

    📄 Loading documents...
    📦 Loaded 1 documents.

    🔍 Vectorizing documents...
    ✅ Vectorization complete.

    📥 4406 chars | "The article is about Paul Graham  What" | [0.0035, -0.012, 0.0051, -0.0273, 0.0032]
    📥 4379 chars | "What  Paul Graham discovered when  Paul" | [0.0261, -0.0076, 0.016, -0.0187, 0.0027]
    📥 4489 chars | "What these programs really showed was th" | [0.0095, -0.0029, 0.0135, -0.0125, 0.018]
    📥 4300 chars | "They either lived long ago or were myste" | [0.001, 0.0044, 0.0168, -0.0277, -0.0027]
    📥 4407 chars | "Only stranieri (foreigners) had to take" | [-0.0043, 0.0203, 0.0295, -0.0284, -0.0152]
    📥 4255 chars | "This is a feature of brains, not a bug." | [0.0003, 0.0225, 0.0305, -0.0256, 0.0078]
    📥 4270 chars | "But the most important thing  Paul Graha" | [0.0135, 0.0014, 0.0257, -0.0287, 0.01]
    📥 4386 chars | "You can do something similar on a map of" | [0.0086, -0.0033, 0.0193, -0.0256, 0.0184]
    📥 4376 chars | "At first this was going to be normal des" | [0.0001, -0.0109, 0.0082, -0.0266, 0.022]
    📥 4306 chars | "It may look clunky today, but in 1996 it" | [-0.0121, -0.0088, -0.0049, -0.0294, 0.0179]
    📥 4272 chars | "The reason  Paul Graham remember learnin" | [-0.0068, -0.0109, 0.0317, -0.0371, -0.0043]
    📥 4322 chars | "At the time  Paul Graham thought Yahoo w" | [0.0047, -0.0097, 0.0371, -0.0307, -0.005]
    📥 4374 chars | "Now  Paul Graham could actually choose w" | [0.0134, 0.0149, 0.0272, -0.0173, 0.0212]
    📥 4408 chars | "Paul Graham certainly did. So at the end" | [-0.0004, 0.0097, 0.0336, -0.0196, 0.016]
    📥 4474 chars | "Over the next several years  Paul Graham" | [0.0039, -0.0068, 0.0178, -0.025, 0.0053]
    📥 4351 chars | "[13]  Once again, ignorance worked in ou" | [0.0172, -0.013, 0.0169, -0.0238, -0.0113]
    📥 4254 chars | "We invited about 20 of the 225 groups to" | [0.0141, -0.0045, 0.0284, -0.0325, 0.0031]
    📥 4172 chars | "When  Paul Graham was dealing with some" | [0.0169, -0.0015, 0.0211, -0.0383, -0.0002]
    📥 4297 chars | "Paul Graham asked Jessica if she wanted" | [0.004, -0.0114, 0.0149, -0.0339, -0.0105]
    📥 4341 chars | "[19]  McCarthy didn't realize this Lisp" | [-0.0075, 0.0027, 0.0117, -0.0302, 0.0194]
    📥 4404 chars | "It felt like  Paul Graham was doing life" | [0.0165, 0.0025, 0.0208, -0.0369, 0.02]
    📥 4549 chars | "But when the software is an online store" | [-0.0013, -0.0024, 0.0235, -0.0112, 0.0028]
    📥 1816 chars | "[17] Another problem with HN was a bizar" | [0.008, 0.0116, 0.0287, -0.0109, -0.0069]

    🎉 Done inserting all data.
    ```

1. Query the LLM with context from the embeddings:

    ```python
    python question.py
    ```

    ```output
    Ask me a question (press Ctrl+C to quit):

    ❓ Your question: tell me about paul graham

    🔍 Retrieved context snippets:
    - 'Over the next several years  Paul Graham' (distance: 0.1471)
    - 'The article is about Paul Graham\n\nWhat  ' (distance: 0.1513)
    - 'Paul Graham certainly did. So at the end' (distance: 0.1523)
    - 'They either lived long ago or were myste' (distance: 0.1530)
    - 'But the most important thing  Paul Graha' (distance: 0.1583)
    - 'You can do something similar on a map of' (distance: 0.1621)
    - 'When  Paul Graham was dealing with some ' (distance: 0.1628)

    💡 Answer:
    Paul Graham is a multifaceted individual who has worked on a variety of projects throughout his life. Before college, he focused on writing and programming. He wrote short stories and began programming on an IBM 1401. He later became more involved with programming when he got a TRS-80 microcomputer.

    In college, he initially planned to study philosophy but switched to AI. He was also interested in art and took art classes at Harvard. He later attended the Accademia di Belli Arti in Florence to study art.

    In his professional life, Graham wrote many essays on different topics, some of which were reprinted in a book called "Hackers & Painters". He also worked on spam filters and did some painting. He used to host dinners for a group of friends every Thursday night and bought a building in Cambridge to use as an office.

    In 2003, he met Jessica Livingston at a party. She was a marketing executive at a Boston investment bank and later decided to compile a book of interviews with startup founders. Graham and Livingston eventually started their own investment firm, implementing ideas they had discussed about venture capital.

    Graham also worked on several projects with Robert Morris and Trevor Blackwell. They collaborated on a new dialect of Lisp, called Arc, and started a company to put art galleries online, which later pivoted to building online stores. This company became Viaweb, which was later sold to Yahoo.

    Graham continued to write essays and work on Y Combinator, a startup accelerator he co-founded. He also developed Hacker News and all of YC's internal software in Arc. In 2013, he decided to hand over Y Combinator to Sam Altman.
    ```

## Review the application

The application is a question-answering system that retrieves relevant text snippets from a PostgreSQL database with vector search support (such as YugabyteDB with pgvector), then uses OpenAI's GPT-4 model to generate an answer based on the retrieved context.

### Imports and setup

```python
import psycopg2        # For connecting to PostgreSQL
import openai          # For interacting with OpenAI API
import os              # For environment variable access
from llama_index.embeddings.openai import OpenAIEmbedding  # For generating embeddings

openai.api_key = os.getenv("OPENAI_API_KEY")  # Load OpenAI key from environment
embed_model = OpenAIEmbedding(model="text-embedding-ada-002")  # Use OpenAI to embed text
client = openai.OpenAI()  # OpenAI client
connection_string = "postgresql://yugabyte:password@127.0.0.1:5433/yugabyte"
```

### ask_question() function

This function is the core logic:

1. Connect to the database:

    ```python
    conn = psycopg2.connect(connection_string)
    ```

1. Embed the question using OpenAI's embedding model:

    ```python
    query_embedding = embed_model.get_query_embedding(question)
    ```

1. Perform a vector similarity search in the vectors table:

    ```sql
    SELECT id, article_text, embedding <=> %s AS distance
    FROM vectors
    ORDER BY embedding <=> %s
    LIMIT %s;
    ```

1. Display snippets of retrieved documents:

    ```python
    print(f"- {text[:40]!r} (distance: {distance:.4f})")
    ```

1. Send the context and question to GPT-4 to generate an answer:

    ```python
    response = client.chat.completions.create(
        model="gpt-4",
        messages=[
            {"role": "system", "content": "..."},
            {"role": "user", "content": f"Context:\n{context}"},
            {"role": "user", "content": f"Question: {question}"}
        ],
    )
    ```

### Interactive loop

Display a prompt for user input, retrieving answers using the `ask_question()` function until interrupted:

```python
if __name__ == "__main__":
    while True:
        question = input("❓ Your question: ").strip()
        ...
```

## Read more

- [Architecting GenAI and RAG Apps with YugabyteDB](https://www.yugabyte.com/ai/)
- [pgvector extension](../../../additional-features/pg-extensions/extension-pgvector/)
