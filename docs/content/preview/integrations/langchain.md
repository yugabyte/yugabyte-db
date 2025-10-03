<!---
title: LangChain Framework
linkTitle: langchain
description: Using Langchain framework with YugabyteDB
aliases:
menu:
  preview_integrations:
    identifier: langchain
    parent: integrations
    weight: 571
type: docs
--->

This docs page covers how to get started with the YugabyteDB as a vector store with LangChain framework for developing Retrieval-Augmented Generation (RAG) apps. 

LangChain is a powerful framework for developing large language model-powered applications, it provides a comprehensive toolkit for building context-aware LLM applications by managing the communication between LLMs and various data sources, including databases and vector stores.

YugabyteDB database brings in the capabilties of `pgvector` extension into the distributed sql architecture providing ultra resilience and seamless scalability for buildling Gen AI applications.

`langchain-yugabytedb` python package provides the capabilities for genai applications to use YugabyteDB database as a vector store using the LangChain framework's vectorstore retrieval for storing and retrieving vector data.

`langchain-yugabytedb` PyPi module [link](https://pypi.org/project/langchain-yugabytedb/).

## Setup

### Minimum Version

langchain-yugabytedb module requires YugabyteDB `v2025.1.0.0` or latest.

### Connecting to YugabyteDB database
 
In order to get started with YugabyteDBVectorStore, lets start a local YugabyteDB node for development purposes -

#### Start YugabyteDB RF-1 Universe

```sh
docker run -d --name yugabyte_node01 --hostname yugabyte01 \
  -p 7000:7000 -p 9000:9000 -p 15433:15433 -p 5433:5433 -p 9042:9042 \
  yugabytedb/yugabyte:2.25.2.0-b359 bin/yugabyted start --background=false \
  --master_flags="allowed_preview_flags_csv=ysql_yb_enable_advisory_locks,ysql_yb_enable_advisory_locks=true" \
  --tserver_flags="allowed_preview_flags_csv=ysql_yb_enable_advisory_locks,ysql_yb_enable_advisory_locks=true"

docker exec -it yugabyte_node01 bin/ysqlsh -h yugabyte01 -c "CREATE extension vector;"
```

For production deployment, performance benchmarking, or deploying a true multi-node on multi-host setup, see Deploy [YugabyteDB](add docs link).


### Installation

```sh
pip install --upgrade --quiet  langchain
pip install --upgrade --quiet  langchain-openai langchain-community tiktoken
pip install --upgrade --quiet  psycopg-binary
pip install -qU "langchain-yugabytedb"
```

### Set your YugabyteDB Values

YugabyteDB clients connect to the cluster using a PostgreSQL compliant connection string. YugabyteDB connection parameters are provided below.

```sh
YUGABYTEDB_USER = "yugabyte"  # @param {type: "string"}
YUGABYTEDB_PASSWORD = ""  # @param {type: "string"}
YUGABYTEDB_HOST = "localhost"  # @param {type: "string"}
YUGABYTEDB_PORT = "5433"  # @param {type: "string"}
YUGABYTEDB_DB = "yugabyte"  # @param {type: "string"}
```

## Initialization

### Environment Setup

This example uses the OpenAI API through OpenAIEmbeddings. We suggest obtaining an OpenAI API key and export it as an environment variable with the name `OPENAI_API_KEY`

### Connecting to YugabyteDB Universe

```python
from langchain_yugabytedb import YBEngine, YugabyteDBVectorStore
from langchain_openai import OpenAIEmbeddings

TABLE_NAME = "my_doc_collection"
VECTOR_SIZE = 1536

CONNECTION_STRING = (
    f"postgresql+asyncpg://{YUGABYTEDB_USER}:{YUGABYTEDB_PASSWORD}@{YUGABYTEDB_HOST}"
    f":{YUGABYTEDB_PORT}/{YUGABYTEDB_DB}"
)
engine = YBEngine.from_connection_string(url=CONNECTION_STRING)

embeddings = OpenAIEmbeddings()
engine.init_vectorstore_table(
    table_name=TABLE_NAME,
    vector_size=VECTOR_SIZE,
)

yugabyteDBVectorStore = YugabyteDBVectorStore.create_sync(
    engine=engine,
    table_name=TABLE_NAME,
    embedding_service=embeddings,
)
```

## Manage vector store

### Add items to vector store

```python
from langchain_core.documents import Document

docs = [
    Document(page_content="Apples and oranges"),
    Document(page_content="Cars and airplanes"),
    Document(page_content="Train"),
]

yugabyteDBVectorStore.add_documents(docs)
```

### Delete items from vector store

```python
yugabyteDBVectorStore.delete(ids=["275823d2-1a47-440d-904b-c07b132fd72b"])
```

### Update items from vector store

Note: Update operation is not supported by YugabyteDBVectorStore.

## Query vector store

Once your vector store has been created and the relevant documents have been added you will most likely wish to query it during the running of your chain or agent.

### Query directly

Performing a simple similarity search can be done as follows:

```python
query = "I'd like a fruit."
docs = yugabyteDBVectorStore.similarity_search(query)
print(docs)
```

If you want to execute a similarity search and receive the corresponding scores you can run:

```python
query = "I'd like a fruit."
docs = yugabyteDBVectorStore.similarity_search(query, k=1)
print(docs)
```

### Query by turning into retriever

You can also transform the vector store into a retriever for easier usage in your chains.

```python
retriever = yugabyteDBVectorStore.as_retriever(search_kwargs={"k": 1})
retriever.invoke("I'd like a fruit.")
```

## ChatMessageHistory

The chat message history abstraction helps to persist chat message history in a YugabyteDB table. 
- `YugabyteDBChatMessageHistory` is parameterized using a table_name and a session_id. 
- The table_name is the name of the table in the database where the chat messages will be stored. 
- The session_id is a unique identifier for the chat session. It can be assigned by the caller using uuid.uuid4()

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

## RAG Example

One of the primary advantages of the vector stores is to provide contextual data to the LLMs. LLMs often are trained with stale data and might not have the relevant domain specific knowledge which results in halucinations in LLMs responses. Take the following example -

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

The current preview release of YugabyteDB is v2.25.2.0, however LLMs is providing stale information which is 2-3 years old. This is where the vector stores complement the LLMs by providing a way to store and retrive relevant information.


### Construct a RAG for providing contextual information

We will provide the relevant information to the LLMs by reading the YugabyteDB documentation. Let's first read the YugabyteDB docs and add data into YugabyteDB Vectorstore by loading, splitting and chuncking data from a html source. We will then store the vector embeddings generated by OpenAI embeddings into YugabyteDB Vectorstore.

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

#### step 2: Configure the YugabyteDB retriever

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
Now, let's try asking the same question Can you tell me the current preview release version of YugabyteDB? again to the LLM

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

## API reference

For detailed information of all YugabyteDBVectorStore features and configurations head to the langchain-yugabytedb github [repo](https://github.com/yugabyte/langchain-yugabytedb)
