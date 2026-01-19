---
title: Integrate the LangChain Framework
headerTitle: LangChain Framework
linkTitle: LangChain
description: Using the LangChain framework with YugabyteDB
headcontent: Use YugabyteDB as a vector store with LangChain
aliases:
menu:
  preview_integrations:
    identifier: langchain-test
    parent: ai-integration
    weight: 25
type: docs
---

Get started using YugabyteDB as a vector store with [LangChain](https://www.langchain.com) for developing Retrieval-Augmented Generation (RAG) apps.

LangChain is a powerful framework for developing large language model-powered applications. It provides a comprehensive toolkit for building context-aware LLM applications by managing the communication between LLMs and various data sources, including databases and vector stores.

YugabyteDB supports the [pgvector extension](../../additional-features/pg-extensions/extension-pgvector/) in a distributed SQL architecture, providing resilience and seamless scalability for buildling generative AI (GAI) applications.

The `langchain-yugabytedb` Python package provides capabilities for GAI applications to use YugabyteDB as a vector store, using the LangChain framework's vectorstore retrieval for storing and retrieving vector data.

`langchain-yugabytedb` is available as a [PyPi module](https://pypi.org/project/langchain-yugabytedb/).

For detailed information of all `YugabyteDBVectorStore` features and configurations, head to the langchain-yugabytedb [GitHub repo](https://github.com/yugabyte/langchain-yugabytedb).

## Quick Start - Complete Working Example

### Prerequisites

- Python 3.9 or later
- Docker
- Create an [OpenAI API Key](https://platform.openai.com/api-keys). Export it as an environment variable with the name `OPENAI_API_KEY`.

### Step 1: Setup Environment

**Install all dependencies (including the missing `langchain-postgres` package):**

```sh
pip install --upgrade --quiet langchain langchain-openai langchain-community langchain-postgres tiktoken psycopg-binary langchain-yugabytedb
```

**Start YugabyteDB with proper vector extension support:**

```sh
docker run -d --name yugabyte_node01 --hostname yugabyte01 \
  -p 7000:7000 -p 9000:9000 -p 15433:15433 -p 5433:5433 -p 9042:9042 \
  yugabytedb/yugabyte:2.25.2.0-b359 bin/yugabyted start --background=false \
  --master_flags="allowed_preview_flags_csv=ysql_yb_enable_advisory_locks,ysql_yb_enable_advisory_locks=true" \
  --tserver_flags="allowed_preview_flags_csv=ysql_yb_enable_advisory_locks,ysql_yb_enable_advisory_locks=true"
```

**Enable the vector extension and verify it's working:**

```sh
# Enable vector extension
docker exec -it yugabyte_node01 bin/ysqlsh -h yugabyte01 -c "CREATE extension if not exists vector;"

# Verify vector extension is installed
docker exec -it yugabyte_node01 bin/ysqlsh -h yugabyte01 -c "SELECT * FROM pg_extension WHERE extname = 'vector';"

# Test vector functionality
docker exec -it yugabyte_node01 bin/ysqlsh -h yugabyte01 -c "SELECT '[1,2,3]'::vector;"
```

### Step 2: Create a sample langchain-yugabytedb application

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
        "How do vector databases work?"
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
    url = "https://docs.yugabyte.com/preview/releases/ybdb-releases/v2.25/"
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

### Step 3: Run the Example

Execute the script:

```bash
python langchain_example.py
```

**Expected Output:**

```output

Starting LangChain + YugabyteDB Complete Example
============================================================
Setting up connection to YugabyteDB...
Initializing OpenAI embeddings...
Creating vector store table...
Connection setup complete!

Testing basic vector store operations...
Adding test documents...
Added 5 documents to vector store

Testing similarity search...

Query: 'I'd like to eat some fruit'
  Result 1: Apples and oranges are delicious fruits
  Result 2: Vector databases store embeddings for similarity search

Query: 'What can I use to travel long distances?'
  Result 1: Trains are efficient for long-distance travel
  Result 2: Cars and airplanes are modes of transportation

Query: 'Tell me about database technology'
  Result 1: YugabyteDB is a distributed SQL database
  Result 2: Vector databases store embeddings for similarity search

Setting up RAG chain...
RAG chain created successfully!

Testing RAG chain...

Question: What are some examples of fruits?
YugaAI: Based on the context, apples and oranges are mentioned as examples of delicious fruits.

Question: What transportation methods were mentioned?
YugaAI: The context mentions cars, airplanes, and trains as modes of transportation.

Question: What is YugabyteDB?
YugaAI: YugabyteDB is a distributed SQL database.

Question: How do vector databases work?
YugaAI: Vector databases store embeddings for similarity search, which allows for finding similar content based on vector representations.

============================================================
Complete example finished successfully!
You now have a working RAG application with YugabyteDB!
```

## Understanding the Example

Now let's break down how this complete example works:

### Connection Setup

The script starts by setting up the connection to YugabyteDB:

```python
# Connection parameters
YUGABYTEDB_USER = "yugabyte"
YUGABYTEDB_PASSWORD = ""
YUGABYTEDB_HOST = "localhost"
YUGABYTEDB_PORT = "5433"
YUGABYTEDB_DB = "yugabyte"

# Create connection string - using psycopg instead of asyncpg for better vector support
CONNECTION_STRING = (
    f"postgresql+psycopg://{YUGABYTEDB_USER}:{YUGABYTEDB_PASSWORD}@{YUGABYTEDB_HOST}"
    f":{YUGABYTEDB_PORT}/{YUGABYTEDB_DB}"
)
```

### Vector Store Initialization

The script initializes the vector store with OpenAI embeddings:

```python
engine = YBEngine.from_connection_string(url=CONNECTION_STRING)
embeddings = OpenAIEmbeddings(api_key=api_key)
engine.init_vectorstore_table(table_name=TABLE_NAME, vector_size=VECTOR_SIZE)
vectorstore = YugabyteDBVectorStore.create_sync(
    engine=engine,
    table_name=TABLE_NAME,
    embedding_service=embeddings,
)
```

### Basic Operations

The script demonstrates adding documents and performing similarity searches:

```python
# Add documents
docs = [
    Document(page_content="Apples and oranges are delicious fruits"),
    Document(page_content="Cars and airplanes are modes of transportation"),
    # ... more documents
]
vectorstore.add_documents(docs)

# Perform similarity search
query = "I'd like to eat some fruit"
results = vectorstore.similarity_search(query)
```

### RAG Chain Creation

The script creates a complete RAG (Retrieval-Augmented Generation) chain:

```python
retriever = vectorstore.as_retriever(search_kwargs={"k": 3})
llm = ChatOpenAI(model="gpt-3.5-turbo", temperature=0, api_key=api_key)

prompt = ChatPromptTemplate.from_messages([
    ("system", "You are a helpful assistant..."),
    ("human", "Context: {context}\nQuestion: {question}")
])

rag_chain = (
    {"context": retriever, "question": RunnablePassthrough()}
    | prompt
    | llm
    | StrOutputParser()
)
```

## Troubleshooting

### Common Issues

1. **Vector extension not working**: Make sure you ran all the verification commands
2. **Connection issues**: Verify YugabyteDB is running and accessible on localhost:5433
3. **API key issues**: Ensure your OpenAI API key is set correctly
4. **Driver compatibility**: Use `psycopg` instead of `asyncpg` for better vector support

### Verification Steps

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

## Detailed API Reference

Now let's explore the individual components in detail:

## Manage vector store

### Add items to the vector store

```python
from langchain_core.documents import Document

docs = [
    Document(page_content="Apples and oranges"),
    Document(page_content="Cars and airplanes"),
    Document(page_content="Train"),
]

yugabyteDBVectorStore.add_documents(docs)
```

### Delete items from the vector store

```python
yugabyteDBVectorStore.delete(ids=["275823d2-1a47-440d-904b-c07b132fd72b"])
```

### Update items in the vector store

Note that the Update operation is not currently supported by YugabyteDBVectorStore.

## Query the vector store

Once your vector store has been created and the relevant documents have been added, you will likely wish to query it during the running of your chain or agent.

### Query directly

Perform a basic similarity search as follows:

```python
query = "I'd like a fruit."
docs = yugabyteDBVectorStore.similarity_search(query)
print(docs)
```

To execute a similarity search and receive the corresponding scores, run the following:

```python
query = "I'd like a fruit."
docs = yugabyteDBVectorStore.similarity_search(query, k=1)
print(docs)
```

### Query by turning into retriever

You can also transform the vector store into a retriever for easier use in your chains.

```python
retriever = yugabyteDBVectorStore.as_retriever(search_kwargs={"k": 1})
retriever.invoke("I'd like a fruit.")
```

## ChatMessageHistory

The chat message history abstraction helps to persist chat message history in a YugabyteDB table.`YugabyteDBChatMessageHistory` is parameterized using a `table_name` and a `session_id`:

- `table_name` is the name of the table in the database where the chat messages will be stored.
- `session_id` is a unique identifier for the chat session. It can be assigned by the caller using `uuid.uuid4()`.

```python
import uuid

from langchain_core.messages import SystemMessage, AIMessage, HumanMessage
from langchain_yugabytedb import YugabyteDBChatMessageHistory
import psycopg

# Establish a synchronous connection to the database
# (or use psycopg.AsyncConnection for async)
conn_info = "dbname=yugabyte user=yugabyte host=localhost port=5433"
sync_connection = psycopg.connect(conn_info)

# Create the table schema (only needs to be done once)
table_name = "chat_history"
YugabyteDBChatMessageHistory.create_tables(sync_connection, table_name)

session_id = str(uuid.uuid4())

# Initialize the chat history manager
chat_history = YugabyteDBChatMessageHistory(
    table_name, session_id, sync_connection=sync_connection
)

# Add messages to the chat history
chat_history.add_messages(
    [
        SystemMessage(content="Meow"),
        AIMessage(content="woof"),
        HumanMessage(content="bark"),
    ]
)

print(chat_history.messages)
```

## RAG example

One of the primary advantages of vector stores is they provide contextual data to LLMs. LLMs often are trained with stale data and might not have the relevant domain specific knowledge, resulting in halucinations in LLM responses.

Take the following example:

```python
import getpass
from langchain_openai import ChatOpenAI
from langchain_core.messages import HumanMessage, SystemMessage, AIMessage

my_api_key = getpass.getpass("Enter your API Key: ")

llm = ChatOpenAI(model="gpt-3.5-turbo", temperature=0.7, api_key=my_api_key)
# Start with a system message to set the persona/behavior of the AI
messages = [
    SystemMessage(
        content="You are a helpful and friendly assistant named 'YugaAI'. You love to answer questions about YugabyteDB and distributed sql."
    ),
    # First human turn
    HumanMessage(content="Hi YugaAI! Where's the headquarters of YugabyteDB?"),
]

print("--- First Interaction ---")
print(f"Human: {messages[1].content}")  # Print the human message
response1 = llm.invoke(messages)
print(f"YugaAI: {response1.content}")

print("\n--- Second Interaction ---")
print(f"Human: {messages[2].content}")  # Print the new human message
response2 = llm.invoke(messages)  # Send the *entire* message history
print(f"YugaAI: {response2.content}")

# Add the second AI response to the history
messages.append(AIMessage(content=response2.content))

# --- 5. Another Turn with a different topic ---
messages.append(
    HumanMessage(
        content="Can you tell me the current preview release version of YugabyteDB?"
    )
)

print("\n--- Third Interaction ---")
print(f"Human: {messages[4].content}")  # Print the new human message
response3 = llm.invoke(messages)  # Send the *entire* message history
print(f"YugaAI: {response3.content}")
```

The current preview release of YugabyteDB is v2.25.2.0, however the LLM is providing stale information that is 2-3 years old. This is where the vector stores complement the LLMs by providing a way to store and retrive relevant information.

### Construct a RAG to provide contextual information

You can provide the relevant information to the LLM by providing the YugabyteDB documentation. First read the YugabyteDB docs and add data into the YugabyteDB vectorstore by loading, splitting, and chuncking data from an HTML source. Then store the vector embeddings generated by OpenAI embeddings into the YugabyteDB vectorstore.

#### Step 1: Generate Embeddings

```python
import getpass
from langchain_community.document_loaders import WebBaseLoader
from langchain_text_splitters import CharacterTextSplitter
from langchain_yugabytedb import YBEngine, YugabyteDBVectorStore
from langchain_openai import OpenAIEmbeddings

my_api_key = getpass.getpass("Enter your API Key: ")
url = "https://docs.yugabyte.com/preview/releases/ybdb-releases/v2.25/"

loader = WebBaseLoader(url)

documents = loader.load()

print(f"Number of documents loaded: {len(documents)}")

# For very large HTML files, you'll want to split the text into smaller
# chunks before sending them to an LLM, as LLMs have token limits.
for i, doc in enumerate(documents):
    text_splitter = CharacterTextSplitter(
        separator="\n\n",  # Split by double newline (common paragraph separator)
        chunk_size=1000,  # Each chunk will aim for 1000 characters
        chunk_overlap=200,  # Allow 200 characters overlap between chunks
        length_function=len,
        is_separator_regex=False,
    )

    # Apply the splitter to the loaded documents
    chunks = text_splitter.split_documents(documents)

    print(f"\n--- After Splitting ({len(chunks)} chunks) ---")

    CONNECTION_STRING = "postgresql+psycopg://yugabyte:@localhost:5433/yugabyte"
    TABLE_NAME = "yb_relnotes_chunks"
    VECTOR_SIZE = 1536
    engine = YBEngine.from_connection_string(url=CONNECTION_STRING)
    engine.init_vectorstore_table(
        table_name=TABLE_NAME,
        vector_size=VECTOR_SIZE,
    )
    embeddings = OpenAIEmbeddings(api_key=my_api_key)

    # The PGVector.from_documents method handles:
    # 1. Creating the table if it doesn't exist (with 'embedding' column).
    # 2. Generating embeddings for each chunk using the provided embeddings model.
    # 3. Inserting the chunk text, metadata, and embeddings into the table.
    vectorstore = YugabyteDBVectorStore.from_documents(
        engine=engine, table_name=TABLE_NAME, documents=chunks, embedding=embeddings
    )

    print(f"Successfully stored {len(chunks)} chunks in PostgreSQL table: {TABLE_NAME}")
```

#### Step 2: Configure the YugabyteDB retriever

```python
from langchain_core.prompts import ChatPromptTemplate
from langchain_core.runnables import RunnablePassthrough
from langchain_core.output_parsers import StrOutputParser
from langchain_openai import ChatOpenAI

retriever = vectorstore.as_retriever(search_kwargs={"k": 3})
print(
    f"Retriever created, set to retrieve top {retriever.search_kwargs['k']} documents."
)

# Initialize the Chat Model (e.g., OpenAI's GPT-3.5 Turbo)
llm = ChatOpenAI(model="gpt-3.5-turbo", temperature=0, api_key=my_api_key)

# Define the RAG prompt template
prompt = ChatPromptTemplate.from_messages(
    [
        (
            "system",
            "You are a helpful and friendly assistant named 'YugaAI'. You love to answer questions about YugabyteDB and distributed sql.",
        ),
        ("human", "Context: {context}\nQuestion: {question}"),
    ]
)
# Build the RAG chain
# 1. Take the input question.
# 2. Pass it to the retriever to get relevant documents.
# 3. Format the documents into a string for the context.
# 4. Pass the context and question to the prompt template.
# 5. Send the prompt to the LLM.
# 6. Parse the LLM's string output.
rag_chain = (
    {"context": retriever, "question": RunnablePassthrough()}
    | prompt
    | llm
    | StrOutputParser()
)
```

Now try asking the same question "Can you tell me the current preview release version of YugabyteDB?" again.

```python
# Invoke the RAG chain with a question
rag_query = "Can you tell me the current preview release version of YugabyteDB?"
print(f"\nQuerying RAG chain: '{rag_query}'")
rag_response = rag_chain.invoke(rag_query)
print("\n--- RAG Chain Response ---")
print(rag_response)
```

```text
Querying RAG chain: 'Can you tell me the current preview release version of YugabyteDB?'
```

```text
--- RAG Chain Response ---
The current preview release version of YugabyteDB is v2.25.2.0.
```

## Learn more

- [Develop applications with AI and YugabyteDB](../../develop/tutorials/AI/)