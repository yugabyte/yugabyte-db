---
title: Integrate the LangChain Framework
headerTitle: LangChain Framework
linkTitle: LangChain
description: Using the LangChain framework with YugabyteDB
headcontent: Use YugabyteDB as a vector store with LangChain
aliases:
menu:
  stable_integrations:
    identifier: langchain-test
    parent: ai-integration
    weight: 25
type: docs
---

[LangChain](https://www.langchain.com) is a powerful framework for developing large language model-powered applications. It provides a comprehensive toolkit for building context-aware LLM applications by managing the communication between LLMs and various data sources, including databases and vector stores.

YugabyteDB supports the [pgvector extension](../../additional-features/pg-extensions/extension-pgvector/) in a distributed SQL architecture, providing resilience and seamless scalability for buildling generative AI (GAI) applications.

The [langchain-yugabytedb](https://pypi.org/project/langchain-yugabytedb/) Python package (available as a PyPi module) provides capabilities for Gen-AI applications to use YugabyteDB as a vector store, using the LangChain framework's vector store retrieval for storing and retrieving vector data.

For detailed information of all `YugabyteDBVectorStore` features and configurations, see the [langchain-yugabytedb GitHub repository](https://github.com/yugabyte/langchain-yugabytedb).

## Example

This example demonstrates a complete RAG (Retrieval-Augmented Generation) application that uses YugabyteDB as a vector store with LangChain. The application follows a three-stage pipeline:

1. Storage: Documents are converted into vector embeddings using OpenAI's embedding model and stored in YugabyteDB. Each document becomes a 1536-dimensional vector that captures its semantic meaning.

1. Retrieval: When a user asks a question, the query is also converted to an embedding. YugabyteDB's vector similarity search finds the most relevant documents from the stored collection based on semantic similarity, not just keyword matching.

1. Generation: The retrieved documents provide context to an LLM (GPT-3.5-turbo), which generates an answer based on the relevant information found in your database, rather than relying solely on its training data.

The example progresses from basic operations (storing and searching documents) to building a complete RAG chain that combines retrieval and generation. Optionally, it can load real YugabyteDB documentation from the web, split it into manageable chunks, and add it to the vector store to create a knowledge base that the LLM can query.

### Prerequisites

- Python 3.9 or later
- Docker
- Create an [OpenAI API key](https://platform.openai.com/api-keys). Export it as an environment variable with the name `OPENAI_API_KEY`.

### Setup

1. Create a virtual environment:

    ```sh
    python3 -m venv .venv
    source .venv/bin/activate
    ```

1. Install all dependencies, including the `langchain-postgres` package:

    ```sh
    pip install --upgrade --quiet langchain langchain-openai langchain-community langchain-postgres tiktoken psycopg-binary langchain-yugabytedb beautifulsoup4
    ```

1. Start YugabyteDB with vector extension support:

    ```sh
    docker run -d --name yugabyte_node01 --hostname yugabyte01 \
    -p 7000:7000 -p 9000:9000 -p 15433:15433 -p 5433:5433 -p 9042:9042 \
    yugabytedb/yugabyte:2.25.2.0-b359 bin/yugabyted start --background=false
    ```

1. Enable the vector extension and verify it's working:

    ```sh
    # Enable vector extension
    docker exec -it yugabyte_node01 bin/ysqlsh -h yugabyte01 -c "CREATE extension if not exists vector;"

    # Verify vector extension is installed
    docker exec -it yugabyte_node01 bin/ysqlsh -h yugabyte01 -c "SELECT * FROM pg_extension WHERE extname = 'vector';"

    # Test vector functionality
    docker exec -it yugabyte_node01 bin/ysqlsh -h yugabyte01 -c "SELECT '[1,2,3]'::vector;"
    ```

### Create a sample langchain-yugabytedb application

Create a file called `langchain_example.py` with the following complete code:

```python
#!/usr/bin/env python3
"""
Complete LangChain + YugabyteDB Integration Example
This script demonstrates a full end-to-end RAG application.
"""

import os
import getpass
from langchain_yugabytedb import YBEngine, YugabyteDBVectorStore
from langchain_openai import OpenAIEmbeddings, ChatOpenAI
from langchain_core.documents import Document
from langchain_community.document_loaders import WebBaseLoader
from langchain_text_splitters import CharacterTextSplitter
from langchain_core.prompts import ChatPromptTemplate
from langchain_core.runnables import RunnablePassthrough
from langchain_core.output_parsers import StrOutputParser

def setup_connection():
    """Set up connection parameters and initialize the vector store."""
    print("Setting up connection to YugabyteDB...")

    # Connection parameters
    YUGABYTEDB_USER = "yugabyte"
    YUGABYTEDB_PASSWORD = ""
    YUGABYTEDB_HOST = "localhost"
    YUGABYTEDB_PORT = "5433"
    YUGABYTEDB_DB = "yugabyte"

    # Table and vector parameters
    TABLE_NAME = "yugabyte_docs_collection"
    VECTOR_SIZE = 1536

    # Create connection string - using psycopg instead of asyncpg for better vector support
    CONNECTION_STRING = (
        f"postgresql+psycopg://{YUGABYTEDB_USER}:{YUGABYTEDB_PASSWORD}@{YUGABYTEDB_HOST}"
        f":{YUGABYTEDB_PORT}/{YUGABYTEDB_DB}"
    )

    # Initialize engine
    engine = YBEngine.from_connection_string(url=CONNECTION_STRING)

    # Initialize embeddings
    print("Initializing OpenAI embeddings...")
    api_key = os.getenv("OPENAI_API_KEY")
    if not api_key:
        api_key = getpass.getpass("Enter your OpenAI API Key: ")

    embeddings = OpenAIEmbeddings(api_key=api_key)

    # Initialize vector store table
    print("Creating vector store table...")
    engine.init_vectorstore_table(
        table_name=TABLE_NAME,
        vector_size=VECTOR_SIZE,
    )

    # Create vector store
    vectorstore = YugabyteDBVectorStore.create_sync(
        engine=engine,
        table_name=TABLE_NAME,
        embedding_service=embeddings,
    )

    print("Connection setup complete!")
    return vectorstore, embeddings, api_key

def test_basic_operations(vectorstore):
    """Test basic vector store operations."""
    print("\nTesting basic vector store operations...")

    # Add test documents
    print("Adding test documents...")
    docs = [
        Document(page_content="Apples and oranges are delicious fruits"),
        Document(page_content="Cars and airplanes are modes of transportation"),
        Document(page_content="Trains are efficient for long-distance travel"),
        Document(page_content="YugabyteDB is a distributed SQL database"),
        Document(page_content="Vector databases store embeddings for similarity search"),
        Document(page_content="CDC Logical Replication support, Colocated tables with tablespaces, Auto Analyze service are some features that went GA in 2025.2.0.0."),
    ]

    vectorstore.add_documents(docs)
    print(f"Added {len(docs)} documents to vector store")

    # Test similarity search
    print("\nTesting similarity search...")
    queries = [
        "I'd like to eat some fruit",
        "What can I use to travel long distances?",
        "Tell me about database technology"
    ]

    for query in queries:
        print(f"\nQuery: '{query}'")
        results = vectorstore.similarity_search(query, k=2)
        for i, doc in enumerate(results, 1):
            print(f"  Result {i}: {doc.page_content}")

    return vectorstore

def create_rag_chain(vectorstore, api_key):
    """Create a RAG chain for advanced question answering."""
    print("\nSetting up RAG chain...")

    # Create retriever
    retriever = vectorstore.as_retriever(search_kwargs={"k": 3})

    # Initialize LLM
    llm = ChatOpenAI(model="gpt-3.5-turbo", temperature=0, api_key=api_key)

    # Define RAG prompt template
    prompt = ChatPromptTemplate.from_messages([
        ("system", "You are a helpful assistant named 'YugaAI'. Answer questions based on the provided context. If you don't know the answer based on the context, say so."),
        ("human", "Context: {context}\n\nQuestion: {question}")
    ])

    # Build RAG chain
    rag_chain = (
        {"context": retriever, "question": RunnablePassthrough()}
        | prompt
        | llm
        | StrOutputParser()
    )

    print("RAG chain created successfully!")
    return rag_chain

def test_rag_chain(rag_chain):
    """Test the RAG chain with various questions."""
    print("\nTesting RAG chain...")

    test_questions = [
        "What are some examples of fruits?",
        "What transportation methods were mentioned?",
        "What is YugabyteDB?",
        "How do vector databases work?",
        "Can you tell me all the features which went GA in 2025.2.0.0 release?"
    ]

    for question in test_questions:
        print(f"\nQuestion: {question}")
        try:
            response = rag_chain.invoke(question)
            print(f"YugaAI: {response}")
        except Exception as e:
            print(f"Error: {e}")

def load_yugabyte_docs(vectorstore, embeddings, api_key):
    """Load YugabyteDB documentation and create a more comprehensive vector store."""
    print("\nLoading YugabyteDB documentation...")

    # Load documentation from web
    url = "https://docs.yugabyte.com/stable/releases/ybdb-releases/v2025.2/#v2025.2.0.0"
    loader = WebBaseLoader(url)
    documents = loader.load()

    print(f"Loaded {len(documents)} documents from web")

    # Split documents into chunks
    text_splitter = CharacterTextSplitter(
        separator="\n\n",
        chunk_size=1000,
        chunk_overlap=200,
        length_function=len,
        is_separator_regex=False,
    )

    chunks = text_splitter.split_documents(documents)
    print(f"Split into {len(chunks)} chunks")

    # Add chunks to vector store
    print("Adding documentation chunks to vector store...")
    vectorstore.add_documents(chunks)
    print(f"Added {len(chunks)} documentation chunks")

    return vectorstore

def main():
    """Main function to run the complete example."""
    print("Starting LangChain + YugabyteDB Complete Example")
    print("=" * 60)

    try:
        # Step 1: Setup
        vectorstore, embeddings, api_key = setup_connection()

        # Step 2: Test basic operations
        vectorstore = test_basic_operations(vectorstore)

        # Step 3: Create RAG chain
        rag_chain = create_rag_chain(vectorstore, api_key)

        # Step 4: Test RAG chain
        test_rag_chain(rag_chain)

        # Step 5: Load additional documentation (optional)
        print("\n" + "=" * 60)
        print("OPTIONAL: Loading YugabyteDB documentation...")
        try:
            vectorstore = load_yugabyte_docs(vectorstore, embeddings, api_key)
            print("Documentation loaded successfully!")
            print("\nTesting with documentation...")
            test_rag_chain(rag_chain)
        except Exception as e:
            print(f"Could not load documentation: {e}")
            print("Continuing with basic example...")

        print("\n" + "=" * 60)
        print("Complete example finished successfully!")
        print("You now have a working RAG application with YugabyteDB!")

    except Exception as e:
        print(f"Error: {e}")
        print("Please check your setup and try again.")

if __name__ == "__main__":
    main()
```

### Run the example

Execute the script:

```sh
python langchain_example.py
```

You should see output similar to the following:

```output
Starting LangChain + YugabyteDB Complete Example
============================================================
Setting up connection to YugabyteDB...
Initializing OpenAI embeddings...
Creating vector store table...
Connection setup complete!

Testing basic vector store operations...
Adding test documents...
Added 6 documents to vector store

Testing similarity search...

Query: 'I'd like to eat some fruit'
  Result 1: Apples and oranges are delicious fruits
  Result 2: Cars and airplanes are modes of transportation

Query: 'What can I use to travel long distances?'
  Result 1: Trains are efficient for long-distance travel
  Result 2: Cars and airplanes are modes of transportation

Query: 'Tell me about database technology'
  Result 1: YugabyteDB is a distributed SQL database
  Result 2: CDC Logical Replication support, Colocated tables with tablespaces, Auto Analyze service are some features that went GA in 2025.2.0.0.

Setting up RAG chain...
RAG chain created successfully!

Testing RAG chain...

Question: What are some examples of fruits?
YugaAI: Based on the provided context, some examples of fruits are apples and oranges.

Question: What transportation methods were mentioned?
YugaAI: The transportation methods mentioned are cars, airplanes, and trains.

Question: What is YugabyteDB?
YugaAI: YugabyteDB is a distributed SQL database.

Question: How do vector databases work?
YugaAI: Vector databases store embeddings for similarity search.

Question: Can you tell me all the features which went GA in 2025.2.0.0 release?
YugaAI: The features that went GA in the 2025.2.0.0 release are:
1. CDC Logical Replication support
2. Colocated tables with tablespaces
3. Auto Analyze service

============================================================
OPTIONAL: Loading YugabyteDB documentation...

Loading YugabyteDB documentation...
Loaded 1 documents from web
Created a chunk of size 16341, which is longer than the specified 1000
Created a chunk of size 15190, which is longer than the specified 1000
Created a chunk of size 18691, which is longer than the specified 1000
Created a chunk of size 17885, which is longer than the specified 1000
Created a chunk of size 2814, which is longer than the specified 1000
Split into 16 chunks
Adding documentation chunks to vector store...
Added 16 documentation chunks
Documentation loaded successfully!

Testing with documentation...

Testing RAG chain...

Question: What are some examples of fruits?
YugaAI: Based on the provided context, some examples of fruits are apples and oranges.

Question: What transportation methods were mentioned?
YugaAI: The transportation methods mentioned are cars, airplanes, and trains.

Question: What is YugabyteDB?
YugaAI: YugabyteDB is a distributed SQL database.

Question: How do vector databases work?
YugaAI: Vector databases store embeddings for similarity search.

Question: Can you tell me all the features which went GA in 2025.2.0.0 release?
YugaAI: The features that went GA (General Availability) in the 2025.2.0.0 release are:
1. Expands ALTER TABLE functionality to include new and ongoing requests.
2. Simplifies ongoing and new tasks in the Catalog Caching area excluding main projects like Hybrid Tables/per-node-catalog cache.
3. Enhances query execution with foreign key performance, further IN batching, distinct and domain type pushdown improvements.
4. Introduces transaction-related enhancements for v2025.2, fixing consistency issues and read-restart errors.
5. Removes table tombstone check for colocated tables, increasing read and write performance.

============================================================
Complete example finished successfully!
You now have a working RAG application with YugabyteDB!
```

## Troubleshooting

### Common issues

- Vector extension not working: Make sure you ran all the verification commands.
- Connection issues: Verify YugabyteDB is running and accessible on `localhost:5433`.
- API key issues: Ensure your OpenAI API key is set correctly.
- Driver compatibility: Use `psycopg` instead of `asyncpg` for better vector support.

### Verification steps

Check if everything is working:

```bash
# Check if YugabyteDB is running
docker ps | grep yugabyte

# Check if vector extension is installed
docker exec -it yugabyte_node01 bin/ysqlsh -h yugabyte01 -c "SELECT * FROM pg_extension WHERE extname = 'vector';"

# Test vector functionality
docker exec -it yugabyte_node01 bin/ysqlsh -h yugabyte01 -c "SELECT '[1,2,3]'::vector;"

# Test Python imports
python -c "from langchain_yugabytedb import YBEngine, YugabyteDBVectorStore; print('Imports successful')"
```

## Additional operations

The main example demonstrates the core RAG workflow. The vector store also supports a number of additional features.

### Delete documents from the vector store

Remove documents by their IDs:

```python
# Delete specific documents by their IDs
vectorstore.delete(ids=["275823d2-1a47-440d-904b-c07b132fd72b", "another-id-here"])
```

### Similarity search with scores

Get similarity scores along with the retrieved documents:

```python
# Retrieve documents with similarity scores
query = "I'd like a fruit."
docs_with_scores = vectorstore.similarity_search_with_score(query, k=3)

for doc, score in docs_with_scores:
    print(f"Score: {score}")
    print(f"Content: {doc.page_content}\n")
```

The score represents the distance between the query embedding and the document embedding. Lower scores indicate higher similarity.

### Update documents

Note that the update operation is not currently supported by `YugabyteDBVectorStore`. To update a document, delete the old one and add the updated version.

### Configure retrievers

A retriever is a LangChain interface that provides a standardized way to query your vector store and fetch relevant documents based on a query. It acts as a bridge between your vector store and LangChain chains, converting queries into embeddings and returning the most similar documents. You can convert a vector store into a retriever using `as_retriever()`, which allows you to use it seamlessly in RAG chains and other LangChain workflows.

Customize the retriever with additional search parameters:

```python
# Create a retriever with custom search parameters
retriever = vectorstore.as_retriever(
    search_type="similarity",
    search_kwargs={"k": 5, "score_threshold": 0.7}
)

# Use in a chain
results = retriever.invoke("What is YugabyteDB?")
```

## Chat message history

`YugabyteDBChatMessageHistory` allows you to persist chat conversation history in YugabyteDB, enabling multi-turn conversations with context. This is separate from the vector store functionality and is useful for maintaining conversation state across sessions.

### Setup

```python
import uuid
from langchain_core.messages import SystemMessage, AIMessage, HumanMessage
from langchain_yugabytedb import YugabyteDBChatMessageHistory
import psycopg

# Establish a synchronous connection to the database
conn_info = "dbname=yugabyte user=yugabyte host=localhost port=5433"
sync_connection = psycopg.connect(conn_info)

# Create the table schema (only needs to be done once)
table_name = "chat_history"
YugabyteDBChatMessageHistory.create_tables(sync_connection, table_name)

# Generate a unique session ID
session_id = str(uuid.uuid4())

# Initialize the chat history manager
chat_history = YugabyteDBChatMessageHistory(
    table_name, session_id, sync_connection=sync_connection
)
```

### Add messages

```python
# Add messages to the chat history
chat_history.add_messages(
    [
        SystemMessage(content="You are a helpful assistant."),
        HumanMessage(content="What is YugabyteDB?"),
        AIMessage(content="YugabyteDB is a distributed SQL database."),
    ]
)

# Retrieve all messages
print(chat_history.messages)
```

### Use with RAG chain

Combine chat history with your RAG chain for conversational AI:

```python
# Add user question to history
chat_history.add_user_message("What features are in v2025.2.0.0?")

# Get conversation context
conversation_context = chat_history.messages

# Use RAG chain to get answer
response = rag_chain.invoke("What features are in v2025.2.0.0?")

# Add AI response to history
chat_history.add_ai_message(response)
```

## Learn more

- [Develop applications with AI and YugabyteDB](/stable/develop/AI/)
- [pgvector extension](../../../additional-features/pg-extensions/extension-pgvector/)
