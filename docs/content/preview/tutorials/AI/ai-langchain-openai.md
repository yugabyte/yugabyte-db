---
title: How to Develop LLM Apps with LangChain, OpenAI and YugabyteDB
headerTitle: Query without SQL using LangChain
linkTitle: Query without SQL - LangChain
description: Learn to build context-aware LLM applications using LangChain and OpenAI.
image: /images/tutorials/ai/icons/langchain-icon.svg
headcontent: Query your database using natural language
menu:
  preview_tutorials:
    identifier: tutorials-ai-langchain-openai
    parent: tutorials-ai-agentic
    weight: 60
type: docs
---

In this tutorial, you build an intelligent application using [LangChain](https://python.langchain.com/docs/get_started/introduction) and [OpenAI](https://openai.com/) that lets you query your database with plain text - no SQL required. This means you can ask _Find me shoes that are in stock and available in size 15_ and instantly get the right answer without ever writing a single SQL query. Behind the scenes, the application takes your natural language input, translates it into the correct SQL query, executes it against your database, and then delivers the results back to you.

While this tutorial demonstrates the solution using YugabyteDB as the database backend, the same approach works seamlessly with standard PostgreSQL and other relational databases. LangChain, a powerful framework for developing language model–powered applications, makes this intuitive interaction possible by bridging the gap between natural language and structured queries.

Follow the guide to learn how to build a scalable LLM application using the [SQL](https://python.langchain.com/docs/use_cases/qa_structured/sql) chain capabilities of LangChain in conjunction with an OpenAI LLM to query a YugabyteDB database using natural language.

## Prerequisites

* Python 3.9 or later
* Docker

## Set up the application

Download the application and provide settings specific to your deployment:

1. Clone the repository.

    ```sh
    git clone https://github.com/YugabyteDB-Samples/yugabytedb-langchain-openai-shoe-store-search.git
    ```

1. Install the application dependencies.

    Dependencies can be installed in a virtual environment, or globally on your machine.

    * Option 1 (recommended): Install Dependencies from *requirements.txt* in virtual environment:

        ```sh
        python3 -m venv yb-langchain-env
        source yb-langchain-env/bin/activate
        pip install -r requirements.txt
        # NOTE: Users with M1 Mac machines should use requirements-m1.txt instead:
        # pip install -r requirements-m1.txt
        ```

    * Option 2: Install Dependencies Globally:

        ```sh
        pip install langchain
        pip install psycopg2
        # NOTE: Users with M1 Mac machines should install the psycopg2 binary instead:
        # pip install psycopg2-binary
        pip install langchain_openai
        pip install langchain_experimental
        pip install flask
        pip install python-dotenv
        ```

1. Create an [OpenAI API Key](https://platform.openai.com/api-keys) and store its value in a secure location. This will be used to connect the application to the LLM to generate SQL queries and an appropriate response from the database.

1. Configure the application environment variables in `{project_directory/.env}`.

## Set up YugabyteDB

Start a 3-node YugabyteDB cluster in Docker (or feel free to use another deployment option):

```sh
# NOTE: if the ~/yb_docker_data already exists on your machine, delete and re-create it
mkdir ~/yb_docker_data

docker network create custom-network

docker run -d --name yugabytedb-node1 --net custom-network \
    -p 15433:15433 -p 7001:7000 -p 9001:9000 -p 5433:5433 \
    -v ~/yb_docker_data/node1:/home/yugabyte/yb_data --restart unless-stopped \
    yugabytedb/yugabyte:{{< yb-version version="preview" format="build">}} \
    bin/yugabyted start \
    --base_dir=/home/yugabyte/yb_data --background=false

docker run -d --name yugabytedb-node2 --net custom-network \
    -p 15434:15433 -p 7002:7000 -p 9002:9000 -p 5434:5433 \
    -v ~/yb_docker_data/node2:/home/yugabyte/yb_data --restart unless-stopped \
    yugabytedb/yugabyte:{{< yb-version version="preview" format="build">}} \
    bin/yugabyted start --join=yugabytedb-node1 \
    --base_dir=/home/yugabyte/yb_data --background=false

docker run -d --name yugabytedb-node3 --net custom-network \
    -p 15435:15433 -p 7003:7000 -p 9003:9000 -p 5435:5433 \
    -v ~/yb_docker_data/node3:/home/yugabyte/yb_data --restart unless-stopped \
    yugabytedb/yugabyte:{{< yb-version version="preview" format="build">}} \
    bin/yugabyted start --join=yugabytedb-node1 \
    --base_dir=/home/yugabyte/yb_data --background=false
```

The database connectivity settings are provided in the `{project_dir}/.env` file and do not need to be changed if you started the cluster using the preceding command.

Navigate to the YugabyteDB UI to confirm that the database is up and running, at <http://127.0.0.1:15433>.

## Load the e-commerce schema and seed data

This application requires an e-commerce database with a product catalog and inventory information. This schema includes `products`, `users`, `purchases`, and `product_inventory`. It also creates a read-only user role to prevent any destructive actions while querying the database directly from LangChain.

The `pg_trgm` PostgreSQL extension is installed to execute similarity searches on alphanumeric text.

1. Copy the schema to the first node's Docker container.

    ```sh
    docker cp {project_dir}/sql/schema.sql yugabytedb-node1:/home
    ```

1. Copy the seed data file to the Docker container.

    ```sh
    docker cp {project_dir}/sql/generated_data.sql yugabytedb-node1:/home
    ```

1. Execute the SQL files against the database.

    ```sh
    docker exec -it yugabytedb-node1 bin/ysqlsh -h yugabytedb-node1 -c '\i /home/schema.sql'
    docker exec -it yugabytedb-node1 bin/ysqlsh -h yugabytedb-node1 -c '\i /home/generated_data.sql'
    ```

## Start the application

The Flask server for this application exposes a REST endpoint which returns values from the database.

1. Start the server.

    ```sh
    python3 app.py
    ```

    ```output
    * Running on http://127.0.0.1:8080
    ```

1. Send a POST request to the `/queries` endpoint with a relevant prompt. For instance:

    ```sh
    # What purchases have been made by user1?

    # What colors do the Intelligent Racer come in?

    # How many narrow shoes come in pink?

    # Find me shoes that are in stock and available in size 15.

    curl -X POST http://127.0.0.1:8080/queries -H "Content-Type: application/json" -d '{"user_prompt":"Find me shoes that are in stock and available in size 15."}'
    ```

    ```output.json
    [
      {
        "color": [
          "yellow",
          "blue"
        ],
        "description": null,
        "name": "Efficient Jogger 1",
        "price": "110.08",
        "quantity": 22,
        "width": [
          "narrow",
          "wide"
        ]
      },
      {
        "color": [
          "blue",
          "yellow"
        ],
        "description": null,
        "name": "Efficient Jogger 8",
        "price": "143.63",
        "quantity": 85,
        "width": [
          "wide",
          "narrow"
        ]
      },
      ...
    ]
    ```

## Review the application

Behind the scenes, the application, implemented in the `llm_db.py` code, bridges natural language and SQL in three key stages:

1. Connect to the database and extract metadata.

    The application starts by establishing a connection to your database using the provided credentials. Once connected, it queries the database for metadata, such as table names, column types, and even some sample data. This metadata is essential because it gives the language model insight into the database structure, enabling it to correctly formulate SQL queries later on.

1. Translate natural language to SQL.

    With the metadata in hand, the application then uses a language model via LangChain and OpenAI to interpret the user's natural language input. For example, when a user asks, _Find me shoes that are in stock and available in size 15_, the language model leverages the database schema information to generate a valid SQL query that targets the right tables and fields.

1. Execute the query and deliver the result.

    After formulating the SQL query, the application executes it against the database. The results are then fetched, processed if necessary (to format or filter the output), and delivered back to the user in an easy-to-read format.

LangChain streamlines this entire process by managing the communication between the language model and your database, ensuring that what begins as a plain language question is accurately translated into a precise SQL query that runs against your data.

The Python application relies on a custom prompt to provide additional context to the LLM, in order to generate more relevant responses. For instance, sample queries are provided to help suggest SQL syntax to OpenAI:

```output
...
Query the database using PostgreSQL syntax.

The color and width columns are array types. The name column is of type VARCHAR.
An example query using an array columns would be:
SELECT * FROM products, unnest(color) as col WHERE col::text % SOME_COLOR;
or
SELECT * FROM products, unnest(width) as wid WHERE wid::text % SOME_WIDTH;

An example query using the name column would be:
select * from products where name ILIKE '%input_text%';
...
```

After setting the context, the user input can be used to complete the prompt.

```output
...
Generate a PostgreSQL query using the input: {input_text}.     
Answer should be in the format of a JSON object. This object should have the key "query" with the SQL query and "query_response" as a JSON array of the query response.
```

After generating a prompt, the LLM and YugabyteDB database work in conjunction to generate a response. The application connects to the database, defining the columns to use when executing queries. The OpenAI LLM is instantiated and added to the `SQLDatabaseChain`. This chain is then invoked, generating and executing a SQL query and subsequently returning the data in the format defined in our prompt.

```python
prompt = custom_prompt(user_prompt)

sql_database = SQLDatabase.from_uri(LOCALDB_URL_STRING, include_tables=["products", "users", "purchases", "product_inventory"])

llm = OpenAI(temperature=0, max_tokens=-1)
db_chain = SQLDatabaseChain(llm=llm, database=sql_database, verbose=True, use_query_checker=True,return_intermediate_steps=True)

try:
    nw_ans = db_chain.invoke(prompt)
    result = nw_ans["result"]
    result_json = json.loads(result)

    return result_json["query_response"]
except (Exception, psycopg2.Error) as error:
    print(error)
```

```sh
# SQLDatabaseChain invocation example:
> Entering new SQLDatabaseChain chain...

    Query the database using PostgreSQL syntax.

    Use the shoe_color enum to query the color. Do not query this column with any values not found in the shoe_color enum.
    Use the shoe_width enum to query the width. Do not query this column with any values not found in the shoe_width enum.

    The color and width columns are array types. The name column is of type VARCHAR.
    An example query using an array columns would be:
    SELECT * FROM products, unnest(color) as col WHERE col::text % SOME_COLOR;
    or
    SELECT * FROM products, unnest(width) as wid WHERE wid::text % SOME_WIDTH;

    An example query using the name column would be:
    select * from products where name ILIKE '%input_text%';

    It is not necessary to search on all columns, only those necessary for a query. 
    
    Generate a PostgreSQL query using the input: How many narrow shoes come in pink? How about blue?. 
    
    Answer should be in the format of a JSON object. This object should have the key "query" with the SQL query and "query_response" as a JSON array of the query response.
    
SQLQuery:SELECT COUNT(*) FROM product_inventory WHERE color = 'pink' AND width = 'narrow' UNION ALL SELECT COUNT(*) FROM product_inventory WHERE color = 'blue' AND width = 'narrow'
SQLResult: [(4,), (5,)]
Answer:{"query": "SELECT COUNT(*) FROM product_inventory WHERE color = 'pink' AND width = 'narrow' UNION ALL SELECT COUNT(*) FROM product_inventory WHERE color = 'blue' AND width = 'narrow'", "query_response": [{"count": 4}, {"count": 5}]}
> Finished chain.
```

## Wrap-up

LangChain provides a powerful toolkit to application developers seeking LLM integrations. By connecting to YugabyteDB, users can access data directly, without the need for replication.

For more information about LangChain, see the [LangChain documentation](https://python.langchain.com/docs/get_started/introduction).

If you would like to learn more on integrating OpenAI with YugabyteDB, check out the [Azure OpenAI](../azure-openai/) tutorial.
