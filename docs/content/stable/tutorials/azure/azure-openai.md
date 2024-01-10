---
title: Build Scalable Generative AI Applications with Azure OpenAI and YugabyteDB
headerTitle: Build scalable generative AI applications with Azure OpenAI and YugabyteDB
linkTitle: Azure OpenAI
description: Build scalable generative AI applications with Azure OpenAI and YugabyteDB
image: /images/tutorials/azure/icons/OpenAI-Icon.svg
headcontent: Use YugabyteDB as the database backend for Azure OpenAI applications
menu:
  stable:
    identifier: tutorials-azure-openai
    parent: tutorials-azure
    weight: 40
type: docs
---

This tutorial outlines the steps required to build a scalable, generative AI application using the Azure OpenAI Service and YugabyteDB.

Follow the guide to learn how to programmatically interface with the Azure OpenAI GPT and Embeddings models, store embeddings in YugabyteDB, and perform a similarity search across a distributed YugabyteDB cluster with the [pgvector extension](https://github.com/pgvector/pgvector).

The [sample application](https://github.com/YugabyteDB-Samples/yugabytedb-azure-openai-lodging-service) we will use is a lodging recommendations service for travelers going to San Francisco. It supports two distinct modes:

![Architecture of a lodging recommendation service built on the Azure OpenAI Service and YugabyteDB](/images/tutorials/azure/azure-openai/architecture-diagram.png "Architecture of a lodging recommendation service built on the Azure OpenAI Service and YugabyteDB
")

1. **Azure OpenAI Chat Mode:** In this mode, the Node.js backend leverages one of the Azure OpenAI GPT models to generate lodging recommendations based on the user's prompt.
1. **YugabyteDB Embeddings Mode**: Initially, the backend employs an Azure OpenAI Embeddings model to convert the user's prompt into an embedding (a vectorized representation of the text data). Next, the server does a similarity search in YugabyteDB to find Airbnb properties that match the user's prompt. YugabyteDB takes advantage of the PostgreSQL pgvector extension for the similarity search in the database.

## Prerequisites

- A [Microsoft Azure](http://azure.microsoft.com) subscription
- Access to the [Azure OpenAI Service](https://azure.microsoft.com/en-us/products/ai-services/openai-service) resource
- The latest [Node.js version](https://github.com/nodejs/release#release-schedule)
- The latest version of [Docker](https://docs.docker.com/desktop/)
- A YugabyteDB cluster running [v2.19.2 or later](https://download.yugabyte.com/)
- [ysqlsh](../../../admin/ysqlsh/) or [psql](https://www.postgresql.org/docs/current/app-psql.html)

## Deploy Azure OpenAI models

This application employs both the GPT and Embeddings models to showcase the distinct performance and traits of each for the application's use case.

Follow the [Microsoft OpenAI guide](https://learn.microsoft.com/en-us/azure/ai-services/openai/how-to/create-resource) to:

- Create the Azure OpenAI resource under your subscription.
- Deploy a GPT model of **gpt-3.5-turbo** (or later). Name the deployment `gpt-model`.
- Deploy an Embedding model of **text-embedding-ada-002** version. Name it `embeddings-model`.

After the resources are provisioned, you'll see them in your Azure OpenAI Studio:

![Model deployments list on the Azure OpenAI Service](/images/tutorials/azure/azure-openai/azure-openai-deployments.png "Model deployments list on the Azure OpenAI Service")

## Set up the application

Download the application and provide settings specific to your Azure OpenAI Service deployment:

1. Clone the repository:

    ```sh
    git clone https://github.com/YugabyteDB-Samples/yugabytedb-azure-openai-lodging-service
    ```

1. Initialize the project:

    ```sh
    cd {project_dir}
    npm i
    cd frontend
    npm i
    ```

1. Open the `{project_dir}/application.properties.ini` file and provide the Azure OpenAI specific settings:

    ```config
    AZURE_OPENAI_KEY= # Your Azure OpenAI API key
    AZURE_OPENAI_ENDPOINT= # Your Azure OpenaAI endpoint for the Language APIs
    AZURE_GPT_MODEL_DEPLOYMENT_NAME = gpt-model
    AZURE_EMBEDDING_MODEL_DEPLOYMENT_NAME = embeddings-model
    ```

## Set up YugabyteDB

YugabyteDB introduced support for the PostgreSQL pgvector extension in v2.19.2. This extension makes it possible to use [PostgreSQL](https://www.yugabyte.com/postgresql/) and YugabyteDB as a vectorized database.

Start a 3-node YugabyteDB cluster in Docker (or feel free to use another deployment option):

```sh
mkdir ~/yb_docker_data

docker network create custom-network

docker run -d --name yugabytedb_node1 --net custom-network \
    -p 15433:15433 -p 7001:7000 -p 9001:9000 -p 5433:5433 \
    -v ~/yb_docker_data/node1:/home/yugabyte/yb_data --restart unless-stopped \
    yugabytedb/yugabyte:{{< yb-version version="preview" format="build">}} \
    bin/yugabyted start \
    --base_dir=/home/yugabyte/yb_data --background=true

docker run -d --name yugabytedb_node2 --net custom-network \
    -p 15434:15433 -p 7002:7000 -p 9002:9000 -p 5434:5433 \
    -v ~/yb_docker_data/node2:/home/yugabyte/yb_data --restart unless-stopped \
    yugabytedb/yugabyte:{{< yb-version version="preview" format="build">}} \
    bin/yugabyted start --join=yugabytedb_node1 \
    --base_dir=/home/yugabyte/yb_data --background=true

docker run -d --name yugabytedb_node3 --net custom-network \
    -p 15435:15433 -p 7003:7000 -p 9003:9000 -p 5435:5433 \
    -v ~/yb_docker_data/node3:/home/yugabyte/yb_data --restart unless-stopped \
    yugabytedb/yugabyte:{{< yb-version version="preview" format="build">}} \
    bin/yugabyted start --join=yugabytedb_node1 \
    --base_dir=/home/yugabyte/yb_data --background=true
```

If you're starting the cluster differently, update the following database connectivity settings in the `{project_dir}/application.properties.ini` file:

```conf
DATABASE_HOST=localhost
DATABASE_PORT=5433
DATABASE_NAME=yugabyte
DATABASE_USER=yugabyte
DATABASE_PASSWORD=yugabyte
```

Navigate to the YugabyteDB UI to confirm that the database is up and running, at <http://127.0.0.1:15433>.

![YugabyteDB Cluster Dashboard](/images/tutorials/azure/azure-openai/yb-cluster.png "YugabyteDB Cluster Dashboard")

## Load the Airbnb data set

As long as the application provides a lodging recommendation service for San Francisco, you can leverage a publicly available Airbnb data set with over 7500 relevant listings:

1. Create the `airbnb_listing` table (you can use [ysqlsh](../../../admin/ysqlsh/) or another comparable SQL tool instead of psql):

    ```sh
    psql -h 127.0.0.1 -p 5433 -U yugabyte -d yugabyte {project_dir}/sql/0_airbnb_listings.sql
    ```

1. Load the data set:

    ```sh
    psql -h 127.0.0.1 -p 5433 -U yugabyte
    \copy airbnb_listing from '{project_dir}/sql/sf_airbnb_listings.csv' DELIMITER ',' CSV HEADER; \
    ```

1. Execute the following script to enable the `pgvector` extension and add the `description_embedding` column of the vector type to the `airbnb_listing` table:

    ```sh
    \i {project_dir}/sql/1_airbnb_embeddings.sql
    ```

## Generate embeddings for Airbnb listings

Airbnb listings provide a detailed property description including number of rooms, types of amenities, a location, and other features. That information is stored in the `description` column and is a perfect fit for the similarity search against user prompts. However, to enable the similarity search, each description first needs to be transformed into its vectorized representation.

The application comes with the embeddings generator (`{project_dir}backend/embeddings_generator.js`) that creates embeddings for all Airbnb properties descriptions.

The generator reads the descriptions of the listings and uses the Azure OpenAI Embedding model to generate a description vector that is then stored in the `description_embedding` column of the database:

```javascript
const embeddingResp = await azureOpenAi.getEmbeddings(embeddingModel, description);
const res = await dbClient.query(
  "UPDATE airbnb_listing SET description_embedding = $1 WHERE id = $2",
  ["[" + embeddingResp.data[0].embedding + "]", id]
);
```

Start the generator using the following command:

```sh
node {project_dir}/backend/embeddings_generator.js
```

Note that it can take 10+ minutes to generate embeddings for more than 7500 Airbnb properties. You should see the following message after the process is completed:

```output
Processing rows starting from 34746755
Processed 7551 rows
Processing rows starting from 35291912
Finished generating embeddings for 7551 rows
```

## Start the application

With the Azure OpenAI models deployed and Airbnb data with embeddings loaded in YugabyteDB, start to explore the application:

1. Start the Node.JS backend:

    ```sh
    cd {project_dir}/backend
    npm start
    ```

1. Start the React frontend:

    ```sh
    cd {project_dir}/frontend
    npm start
    ```

The application UI should display, and is available at the address <http://localhost:3000/>.

![Lodging Service](/images/tutorials/azure/azure-openai/azure-openai-lodging-service.png "Lodging Service")

### Explore the Azure OpenAI chat mode

The Azure OpenAI Chat mode of the application relies on the [Azure Chat Completions API](https://learn.microsoft.com/en-us/azure/ai-services/openai/reference#completions). In this mode, the app's behavior is similar to that of ChatGPT. Type in your prompt and it will be sent as-is to the neural network that returns the requested information.

For example, send the following prompt to the GPT model to get a few recommendations:

```sh
I'm looking for an apartment near the Golden Gate Bridge with a nice view of the Bay.
```

![Lodging Service Results](/images/tutorials/azure/azure-openai/azure-openai-lodging-service-results.png "Lodging Service Results")

Internally, the application performs the following steps (see the **{project_dir}/backend/openai_chat_services.js** for details):

1. It prepares system and user messages for the neural network; the system message clarifies what is expected from the GPT model:

    ```javascript
    const messages = [
    {
        role: "system",
        content:
        "You're a helpful assistant that helps to find lodging in San Francisco. Suggest three options. Send back a JSON object in the format below." +
        '[{"name": "&lt;hotel name>", "description": "&lt;hotel description>", "price": &lt;hotel price>}]' +
        "Don't add any other text to the response.",
    },

    {
        role: "user",
        content: prompt,
    },
    ];
    ```

1. The messages are sent to the Azure OpenAI Service:

    ```javascript
    const chatCompletion = await this.#azureOpenAi.getChatCompletions(this.#gptModel, messages);
    ```

1. The application extracts the JSON data with recommendations from the response and returns those recommendations to the React frontend:

    ```javascript
    const message = chatCompletion.choices[0].message.content;
    const jsonStart = message.indexOf("[");
    const jsonEnd = message.indexOf("]");
    const places = JSON.parse(message.substring(jsonStart, jsonEnd + 1));
    return places;
    ```

Depending on the selected model type, your subscription, and the neural network's workload, it can take between 5 and 20 seconds for the Chat Completions API to generate the recommendations.

To make the solution scalable, with the latency for recommendations retrieval coming in at under a second, we need to switch to the app's YugabyteDB Embeddings mode.

### Scale with the YugabyteDB Embeddings mode

If you select the YugabyteDB Embeddings mode from the application UI and send the same prompt, you'll get a different list of suggestions, but at less than one second of latency:

![Lodging Service Results with YugabyteDB Embeddings](/images/tutorials/azure/azure-openai/azure-openai-lodging-service-results-yb.png "Lodging Service Results with YugabyteDB Embeddings")

This mode performs better because the similarity search is done via our own data set stored in the distributed YugabyteDB cluster.

The application performs the following steps to generate the recommendations (see the **{project_dir}/backend/yugabytedb_embeddings_service.js** for details):

1. The application generates a vectorized representation of the user prompt using the Azure OpenAI Embeddings model:

    ```javascript
    const embeddingResp = await this.#azureOpenAi.getEmbeddings(this.#embeddingModel, prompt);
    ```

1. The app uses the generated vector to retrieve the most relevant Airbnb properties stored in YugabyteDB:

    ```javascript
    const res = await this.#dbClient.query(
    "SELECT name, description, price, 1 - (description_embedding <=> $1) as similarity " +
        "FROM airbnb_listing WHERE 1 - (description_embedding <=> $1) > $2 ORDER BY similarity DESC LIMIT $3",
    ["[" + embeddingResp.data[0].embedding + "]", matchThreshold, matchCnt]
    );
    ```

    The similarity is calculated as a cosine distance between the embeddings stored in the `description_embedding` column and the user prompt's vector.

1. The suggested Airbnb properties are returned in the JSON format to the React frontend:

    ```javascript
    for (let i = 0; i < res.rows.length; i++) {
    const row = res.rows[i];

    places.push({
        name: row.name,
        description: row.description,
        price: row.price,
        similarity: row.similarity,
    });
    }
    return places;
    ```

## Wrap-up

The Azure OpenAI Service simplifies the process of designing, building, and productizing generative AI applications by offering developer APIs for various major programming languages.

YugabyteDB enhances the scalability of these applications by distributing data and embeddings across a cluster of nodes, facilitating similarity searches on a large scale.

To learn more about additional updates to YugabyteDB with release 2.19, check out [Dream Big, Go Bigger: Turbocharging PostgreSQL](https://www.yugabyte.com/blog/postgresql-turbocharging/).
