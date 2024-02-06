---
title: How to Develop RAG Apps with LlamaIndex, OpenAI and YugabyteDB
headerTitle: Build RAG applications with LlamaIndex, OpenAI, and YugaybteDB
linkTitle: LlamaIndex and OpenAI
description: Learn to build RAG applications using LlamaIndex and OpenAI.
image: /images/tutorials/ai/icons/llamaindex-icon.svg
headcontent: Use YugaybteDB as the database backend for RAG applications
menu:
  preview:
    identifier: tutorials-ai-llamaindex-openai
    parent: tutorials-ai
    weight: 60
type: docs
---

This tutorial demonstrates how use LlamaIndex to build RAG (Retrieval-Augmented Generation) applications. By using the LlamaIndex [SQLJoinQueryEngine](https://docs.llamaindex.ai/en/stable/examples/query_engine/SQLJoinQueryEngine.html), the application can query a [PostgreSQL-compatible](https://www.yugabyte.com/postgresql/postgresql-compatibility/) YugabyteDB database from natural language. It can then infer whether to query a secondary vector index to fetch documents. In this case, the secondary index contains the Wikipedia pages of S&P 500 companies.

## Prerequisites
* Install Python3
* Install Docker

## Set up the application
Download the application and provide settings specific to your deployment:

1. Clone the repository.

    ```sh
    git clone https://github.com/YugabyteDB-Samples/yugabytedb-llamaindex-sp500-search.git
    ```

2. Install the application dependencies.

    Dependencies can be installed in a virtual environment, or globally on your machine.

    * Option 1 (recommended): Install Dependencies from *requirements.txt* in virtual environment
        ```sh
        python3 -m venv yb-llamaindex-env
        source yb-llamaindex-env/bin/activate
        pip install -r requirements.txt
        # NOTE: Users with M1 Mac machines should use requirements-m1.txt instead:
        # pip install -r requirements-m1.txt
        ```

    * Option 2: Install Dependencies Globally
        ```sh
        pip install llama-index
        pip install psycopg2
        # NOTE: Users with M1 Mac machines should install the psycopg2 binary instead:
        # pip install psycopg2-binary
        pip install python-dotenv
        ```
3. Create an [OpenAI API Key](https://platform.openai.com/api-keys) and store it's value in a secure location. This will be used to connect the application to the LLM to generate SQL queries, infer results and generate the proper response.

4. Configure the application environment variables in `{project_directory/.env}`.

## Get Started with YugabyteDB

YugabyteDB is a [PostgreSQL-compatible](https://www.yugabyte.com/postgresql/postgresql-compatibility/) distributed database.  

Start a 3-node YugabyteDB cluster in Docker (or feel free to use another deployment option):

```sh
# NOTE: if the ~/yb_docker_data already exists on your machine, delete and re-create it
mkdir ~/yb_docker_data

docker network create custom-network

docker run -d --name yugabytedb-node1 --net custom-network \
    -p 15433:15433 -p 7001:7000 -p 9001:9000 -p 5433:5433 \
    -v ~/yb_docker_data/node1:/home/yugabyte/yb_data --restart unless-stopped \
    yugabytedb/yugabyte:2.20.1.0-b97 \
    bin/yugabyted start \
    --base_dir=/home/yugabyte/yb_data --background=false

docker run -d --name yugabytedb-node2 --net custom-network \
    -p 15434:15433 -p 7002:7000 -p 9002:9000 -p 5434:5433 \
    -v ~/yb_docker_data/node2:/home/yugabyte/yb_data --restart unless-stopped \
    yugabytedb/yugabyte:2.20.1.0-b97 \
    bin/yugabyted start --join=yugabytedb-node1 \
    --base_dir=/home/yugabyte/yb_data --background=false

docker run -d --name yugabytedb-node3 --net custom-network \
    -p 15435:15433 -p 7003:7000 -p 9003:9000 -p 5435:5433 \
    -v ~/yb_docker_data/node3:/home/yugabyte/yb_data --restart unless-stopped \
    yugabytedb/yugabyte:2.20.1.0-b97 \
    bin/yugabyted start --join=yugabytedb-node1 \
    --base_dir=/home/yugabyte/yb_data --background=false
```

The database connectivity settings are provided in the `{project_dir}/.env` file and do not need to be changed if you started the cluster with the preceding command.

Navigate to the YugabyteDB UI to confirm that the database is up and running, at <http://127.0.0.1:15433>.

## Load the Financial Schema and Seed Data

This application requires a database table with financial information for companies in the S&P 500. This schema includes a `companies` table. It also creates a read-only user role to prevent any destructive actions while querying the database directly from LlamaIndex.

1. Copy the schema to the first node's Docker container.
    ```sh
    docker cp {project_dir}/sql/schema_simplified.sql yugabytedb-node1:/home
    ```   

2. Copy the seed data file to the Docker container.
    ```sh
    docker cp {project_dir}/sql/data_simplified.sql yugabytedb-node1:/home
    ```

3. Execute the SQL files against the database.
    ```sh
     docker exec -it yugabytedb-node1 bin/ysqlsh -h yugabytedb-node1 -c '\i /home/schema_simplified.sql'
     docker exec -it yugabytedb-node1 bin/ysqlsh -h yugabytedb-node1 -c '\i /home/data_simplified.sql'
    ```

## Start the Application

This command-line application takes an input in natural language and returns a response from LlamaIndex.

1. Start the server.

```sh
python3 index.py
```

```output
What is your question?
```

2. Provide a relevant question. For instance:

```
What is your question? 

Provide a detailed company history for the company with the highest marketcap.
```

```output
Querying SQL database: The first choice seems more relevant as it mentions translating a natural language query into a SQL query over a table containing companies' stats. This could potentially include the company with the highest marketcap and provide a detailed history. The second choice is more about answering semantic questions, which doesn't necessarily imply detailed company history.

SQL query: SELECT * 
FROM companies_simplified 
WHERE marketcap = (SELECT MAX(marketcap) FROM companies_simplified)

SQL response: The company with the highest marketcap is Microsoft Corporation. It was founded on April 4, 1975, and is headquartered at One Microsoft Way, Redmond, Washington, United States. Microsoft is a technology company that specializes in software infrastructure. It has a marketcap of $1,043,526,401,920 and employs 221,000 people. The company's contact number is 425-882-8080.

Transformed query given SQL response: Can you provide more details about the key products and services offered by Microsoft Corporation?

query engine response: Microsoft Corporation offers a wide range of products and services. Some of its key products include operating systems such as Windows, which is used by millions of individuals and businesses worldwide. Microsoft Office Suite is another popular product, which includes applications like Word, Excel, PowerPoint, and Outlook for productivity and communication purposes. The company also offers cloud-based services through its Azure platform, providing infrastructure, analytics, and other solutions for businesses. Additionally, Microsoft develops and sells hardware devices like the Xbox gaming console and Surface tablets. It also provides enterprise software solutions, developer tools, and various other products and services to cater to the needs of different industries and customers.

Final response: The company with the highest market cap is Microsoft Corporation. It was founded on April 4, 1975, and is headquartered at One Microsoft Way, Redmond, Washington, United States. Microsoft is a technology company that specializes in software infrastructure. It has a market cap of $1,043,526,401,920 and employs 221,000 people. The company's contact number is 425-882-8080.

Microsoft Corporation offers a wide range of products and services. Some of its key products include operating systems such as Windows, which is used by millions of individuals and businesses worldwide. Microsoft Office Suite is another popular product, which includes applications like Word, Excel, PowerPoint, and Outlook for productivity and communication purposes. The company also offers cloud-based services through its Azure platform, providing infrastructure, analytics, and other solutions for businesses. Additionally, Microsoft develops and sells hardware devices like the Xbox gaming console and Surface tablets. It also provides enterprise software solutions, developer tools, and various other products and services to cater to the needs of different industries and customers.
```

## Review the Application

The python application relies on both structured and unstructured data to provide an appropriate response. By connecting to the database and providing contextual information, LlamaIndex is able to infer which tables to query and report their columns to the LLM.

```python
...
sql_engine = create_engine(f"postgresql+psycopg2://{DB_USERNAME}:{DB_PASSWORD}@{DB_HOST}:{DB_PORT}/{DB_NAME}")
chunk_size = 1024
llm = OpenAI(temperature=0.1, model="gpt-4", streaming=True)
service_context = ServiceContext.from_defaults(chunk_size=chunk_size, llm=llm)
sql_database = SQLDatabase(sql_engine, include_tables=["companies"])

from llama_index.indices.struct_store.sql_query import NLSQLTableQueryEngine

sql_query_engine = NLSQLTableQueryEngine(
    sql_database=sql_database,
    tables=["companies"],
)

sql_tool = QueryEngineTool.from_defaults(
    query_engine=sql_query_engine,
    description=(
        "Useful for translating a natural language query into a SQL query over"
        " a table containing: companies, containing stats about S&P 500 companies."
    ),
)
```
Additionally, a QueryEngineTool is created for Wikipedia search. This query engine is imported from `wiki_search.py`, which creates a VectorStoreIndex from the documents downloaded from Wikipedia for each S&P 500 company. This index is then stored locally for re-use, preventing the need to index more data and incur more resource costs from the LLM.
```python
# index.py
...
from wiki_search import wiki_query_engine

wiki_tool = QueryEngineTool.from_defaults(
    query_engine=wiki_query_engine,
    description=(
        f"Useful for answering qualitative questions about different S&P 500 companies."
    ),
)
...
```
```python
# wiki_search.py
import wikipedia
from llama_index import download_loader
from llama_index import VectorStoreIndex, StorageContext, load_index_from_storage
WikipediaReader = download_loader("WikipediaReader")

PERSIST_DIR = "./wiki_index"
if not os.path.exists(PERSIST_DIR):
    wiki_pages = []
    for i in range(0, len(symbols)):
        ticker = yf.Ticker(symbols[i])

        if 'longName' in ticker.info:
            print(ticker.info["longName"])
            search_results = wikipedia.search(ticker.info["longName"])[0]
            wiki_pages.append(search_results)

    loader = WikipediaReader()
    # auto_suggest allows wikipedia to change page search string
    documents = loader.load_data(pages=wiki_pages, auto_suggest=False)
    print(len(documents))

    index = VectorStoreIndex.from_documents(documents)
    index.storage_context.persist("wiki_index")
    wiki_query_engine = index.as_query_engine()
    print("Created wiki_query_engine for first time")
else:
    storage_context = StorageContext.from_defaults(persist_dir="wiki_index")
    index = load_index_from_storage(storage_context=storage_context)
    wiki_query_engine = index.as_query_engine()
    print("Loaded wiki_query_engine index from storage")
```

Putting it all together, the tools are combined, allowing LlamaIndex and OpenAI to effectively choose how user questions can be answered most efficiently and accurately.

```python
query_engine = SQLJoinQueryEngine(
    sql_tool, wiki_tool, service_context=service_context
)

query_str = input("What is your question? \n\n")

while query_str != "":
    augmented_query_string = f"Answer this question specifically: {query_str}. Ignore null values."
    response = query_engine.query(augmented_query_string)
    print(response)
    query_str = input("What is your question? \n\n")
```

## Wrap-up
LlamaIndex is a powerful tool for building RAG applications over a variety of indexes. This tutorial merely scratches the surface of what's possible, combining structured and unstructured data to build powerful AI applications.

For more information about LlamaIndex, see the [LlamaIndex documentation](https://docs.llamaindex.ai/en/stable/).

If you would like to learn more on building LLM apps with YugabyteDB, check out the [LangChain and OpenAI](../../ai/ai-langchain-openai/) tutorial.