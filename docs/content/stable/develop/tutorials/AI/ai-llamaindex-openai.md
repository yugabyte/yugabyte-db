---
title: How to Develop RAG Apps with LlamaIndex, OpenAI and YugabyteDB
headerTitle: Talk to a database and knowledge base
linkTitle: Knowledge base - LlamaIndex
description: Learn to build RAG applications using LlamaIndex and OpenAI.
image: /images/tutorials/ai/icons/llamaindex-icon.svg
headcontent: Use YugabyteDB as the database backend for RAG applications
menu:
  stable_develop:
    identifier: tutorials-ai-llamaindex-openai
    parent: tutorials-ai-agentic
    weight: 60
type: docs
---

This tutorial demonstrates how use LlamaIndex to build Retrieval-Augmented Generation (RAG) applications. By using the [LlamaIndex SQLJoinQueryEngine](https://docs.llamaindex.ai/en/stable/examples/query_engine/SQLJoinQueryEngine.html), the application can query a [PostgreSQL-compatible](https://www.yugabyte.com/postgresql/postgresql-compatibility/) YugabyteDB database from natural language. It can then infer whether to query a secondary vector index to fetch documents. In this case, the secondary index contains the Wikipedia pages of S&P 500 companies.

## Prerequisites

* Python 3.9
* Docker

## Set up the application

Download the application and provide settings specific to your deployment:

1. Clone the repository.

    ```sh
    git clone https://github.com/YugabyteDB-Samples/yugabytedb-llamaindex-sp500-search.git
    cd yugabytedb-llamaindex-sp500-search
    ```

1. Install the application dependencies.

    Dependencies can be installed in a virtual environment, or globally on your machine.

    * Option 1 (recommended): Install Dependencies from *requirements.txt* in virtual environment.

        ```sh
        python3 -m venv yb-llamaindex-env
        source yb-llamaindex-env/bin/activate
        pip install -r requirements.txt
        python -m pip install --upgrade yfinance
        ```

    * Option 2: Install Dependencies Globally.

        ```sh
        pip install llama-index
        pip install psycopg2-binary
        pip install python-dotenv
        ```

1. Create an [OpenAI API Key](https://platform.openai.com/api-keys). This will be used to connect the application to the LLM to generate SQL queries, infer results and generate the proper response.

1. Configure the application environment variables in `{project_directory/.env}`.

## Set up YugabyteDB

Start a 3-node YugabyteDB cluster in Docker (or feel free to use another deployment option). (If the `~/yb_docker_data` directory already exists on your machine, delete and re-create it.)

```sh
mkdir ~/yb_docker_data

docker network create yb-network

docker run -d --name ybnode1 --hostname ybnode1 --net yb-network \
    -p 15433:15433 -p 7001:7000 -p 9001:9000 -p 5433:5433 \
    -v ~/yb_docker_data/node1:/home/yugabyte/yb_data --restart unless-stopped \
    yugabytedb/yugabyte:{{< yb-version version="stable" format="build">}} \
    bin/yugabyted start \
    --base_dir=/home/yugabyte/yb_data --background=false

docker run -d --name ybnode2 --hostname ybnode2 --net yb-network \
    -v ~/yb_docker_data/node2:/home/yugabyte/yb_data --restart unless-stopped \
    yugabytedb/yugabyte:{{< yb-version version="stable" format="build">}} \
    bin/yugabyted start --join=ybnode1 \
    --base_dir=/home/yugabyte/yb_data --background=false

docker run -d --name ybnode3 --hostname ybnode3 --net yb-network \
    -v ~/yb_docker_data/node3:/home/yugabyte/yb_data --restart unless-stopped \
    yugabytedb/yugabyte:{{< yb-version version="stable" format="build">}} \
    bin/yugabyted start --join=ybnode1 \
    --base_dir=/home/yugabyte/yb_data --background=false
```

The database connectivity settings are provided in the `{project_dir}/.env` file and do not need to be changed if you started the cluster using the preceding command.

Navigate to the YugabyteDB UI to confirm that the database is up and running, at <http://127.0.0.1:15433>.

## Load the financial schema and seed data

This application requires a database table with financial information for companies in the S&P 500. This schema includes a `companies` table. It also creates a read-only user role to prevent any destructive actions while querying the database directly from LlamaIndex.

1. Copy the schema to the first node's Docker container.

    ```sh
    docker cp {project_dir}/sql/schema_extended.sql ybnode1:/home
    docker cp {project_dir}/sql/schema.sql ybnode1:/home
    ```

1. Copy the seed data file to the Docker container.

    ```sh
    docker cp {project_dir}/sql/data_extended.sql ybnode1:/home
    docker cp {project_dir}/sql/data.sql ybnode1:/home
    ```

1. Execute the SQL files against the database.

    ```sh
    docker exec -it ybnode1 bin/ysqlsh -h ybnode1 -f /home/schema_extended.sql
    docker exec -it ybnode1 bin/ysqlsh -h ybnode1 -f /home/schema.sql
    docker exec -it ybnode1 bin/ysqlsh -h ybnode1 -f /home/data_extended.sql
    docker exec -it ybnode1 bin/ysqlsh -h ybnode1 -f /home/data.sql
    ```

## Start the application

This command-line application takes an input in natural language and returns a response from LlamaIndex.

1. Start the server.

    ```sh
    python3 index.py
    ```

    The first time the application runs, it loads the S&P 500 data. When finished, you should see the following prompt:

    ```output
    What is your question?
    ```

1. Provide a relevant question. For instance:

    ```sh
    Provide a detailed company history for the company with the highest marketcap.
    ```

The AI agent combines insights from YugabyteDB and the Wikipedia vector store to provide an appropriate response.

```output
Querying SQL database: The first choice seems more relevant as it mentions translating a natural language query into a SQL query over a table containing companies' stats. This could potentially include the company with the highest marketcap and provide a detailed history. The second choice is more about answering semantic questions, which doesn't necessarily imply detailed company history.

SQL query: SELECT *
FROM companies
WHERE marketcap = (SELECT MAX(marketcap) FROM companies)

SQL response: The company with the highest marketcap is Microsoft Corporation. It was founded on April 4, 1975, and is headquartered at One Microsoft Way, Redmond, Washington, United States. Microsoft is a technology company that specializes in software infrastructure. It has a marketcap of $1,043,526,401,920 and employs 221,000 people. The company's contact number is 425-882-8080.

Transformed query given SQL response: Can you provide more details about the key products and services offered by Microsoft Corporation?

query engine response: Microsoft Corporation offers a wide range of products and services. Some of its key products include operating systems such as Windows, which is used by millions of individuals and businesses worldwide. Microsoft Office Suite is another popular product, which includes applications like Word, Excel, PowerPoint, and Outlook for productivity and communication purposes. The company also offers cloud-based services through its Azure platform, providing infrastructure, analytics, and other solutions for businesses. Additionally, Microsoft develops and sells hardware devices like the Xbox gaming console and Surface tablets. It also provides enterprise software solutions, developer tools, and various other products and services to cater to the needs of different industries and customers.

Final response: The company with the highest market cap is Microsoft Corporation. It was founded on April 4, 1975, and is headquartered at One Microsoft Way, Redmond, Washington, United States. Microsoft is a technology company that specializes in software infrastructure. It has a market cap of $1,043,526,401,920 and employs 221,000 people. The company's contact number is 425-882-8080.

Microsoft Corporation offers a wide range of products and services. Some of its key products include operating systems such as Windows, which is used by millions of individuals and businesses worldwide. Microsoft Office Suite is another popular product, which includes applications like Word, Excel, PowerPoint, and Outlook for productivity and communication purposes. The company also offers cloud-based services through its Azure platform, providing infrastructure, analytics, and other solutions for businesses. Additionally, Microsoft develops and sells hardware devices like the Xbox gaming console and Surface tablets. It also provides enterprise software solutions, developer tools, and various other products and services to cater to the needs of different industries and customers.
```

## Review the application

The Python application relies on both structured and unstructured data to provide an appropriate response. By connecting to the database and providing contextual information, LlamaIndex is able to infer which tables to query and report their columns to the LLM.

```python
...
# Configure LLM - global settings

Settings.llm = OpenAI(temperature=0.1, model="gpt-4", streaming=True)
Settings.embed_model = OpenAIEmbedding(model="text-embedding-3-small")
Settings.node_parser = SentenceSplitter(chunk_size=512, chunk_overlap=20)
Settings.num_output = 512
Settings.context_window = 3900

# Configure SQL Engine for connecting to YugabyteDB Universe

sql_engine = create_engine(f"postgresql+psycopg2://{DB_USERNAME}:{DB_PASSWORD}@{DB_HOST}:{DB_PORT}/{DB_NAME}")

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

wiki_yugabytedb_pgvector_tool = QueryEngineTool.from_defaults(
    query_engine=wiki_query_engine,
    description=(
        f"Useful for answering qualitative questions about different S&P 500 companies."
    ),
)
...
```

### Use the YugabyteDB Vector index

The `PGVectorStore` object is created to store and retrieve vector indicies from the YugabyteDB universe.

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

    # auto_suggest allows wikipedia to change page search string
    documents = WikipediaReader().load_data(pages=wiki_pages, auto_suggest=False)
    logging.info(f"Wiki pages loaded: ", len(documents))

    # Configure PGVectorStore with YugabyteDB credentials
    vector_store = PGVectorStore.from_params(
        database=DB_NAME,
        host=DB_HOST,
        password=DB_PASSWORD,
        port=DB_PORT,
        user=DB_USERNAME,
        table_name="snp_wiki_embeddings",
        embed_dim=1536
    )

    storage_context_yugabytedb_vs = StorageContext.from_defaults(vector_store=vector_store)
    wiki_vector_index = VectorStoreIndex.from_documents(
        documents, storage_context=storage_context_yugabytedb_vs, show_progress=True
    )
    wiki_query_engine = wiki_vector_index.as_query_engine()

    logging.info("Created wiki_query_engine for the first time")
else:
    storage_context = StorageContext.from_defaults(persist_dir="wiki_index")
    index = load_index_from_storage(storage_context=storage_context)
    wiki_query_engine = index.as_query_engine()
    logging.info("Loaded wiki_query_engine index from storage")
```

Putting it all together, the tools are combined, allowing LlamaIndex and OpenAI to effectively choose how user questions can be answered most efficiently and accurately.

```python
query_engine = SQLJoinQueryEngine(
    sql_tool, wiki_yugabytedb_pgvector_tool
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

If you would like to learn more on building LLM apps with YugabyteDB, check out the [LangChain and OpenAI](../ai-langchain-openai/) tutorial.
