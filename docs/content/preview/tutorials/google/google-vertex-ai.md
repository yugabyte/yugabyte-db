---
title: Build Scalable Generative AI Applications with Google Vertex AI and YugabyteDB
headerTitle: Build scalable generative AI applications with Google Vertex AI and YugabyteDB
linkTitle: Google Vertex AI
description: Build scalable generative AI applications with Google Vertex AI and YugabyteDB
image: /images/tutorials/google/icons/Google-Vertex-AI-Icon.svg
headcontent: Use YugabyteDB as the database backend for Google Vertex AI applications
menu:
  preview:
    identifier: tutorials-google-vertex-ai
    parent: tutorials-google
    weight: 40
type: docs
---

This tutorial outlines the steps required to build a scalable, generative AI application using Google Vertex AI and YugabyteDB.

Follow the guide to learn how to programmatically interface with the Google Vertex AI to generate text embeddings (a vectorized representation of the data) for each listing description, store embeddings in YugabyteDB, and perform a similarity search across a distributed YugabyteDB cluster using the [pgvector extension](https://github.com/pgvector/pgvector).

The [sample application](https://github.com/YugabyteDB-Samples/yugabytedb-azure-openai-lodging-service) we will use is a lodging recommendations service for travelers going to San Francisco.

![YugaLodgings Application](/images/tutorials/google/google-vertex-ai/yugalodgings-main.png "YugaLodgings Application")

## Prerequisites

- A Google Cloud account with appropriate permissions
- A YugabyteDB cluster running [v2.19.2 or later](https://download.yugabyte.com/)
- [Node.js](https://github.com/nodejs/release#release-schedule) v18 or later
- The latest version of [Docker](https://docs.docker.com/desktop/)
- [ysqlsh](../../../admin/ysqlsh/) or [psql](https://www.postgresql.org/docs/current/app-psql.html)

## Set up the application

1. Clone the repository.

    ```sh
    git clone https://github.com/YugabyteDB-Samples/yugabytedb-google-vertexai-lodging-service.git
    ```

1. Install the application dependencies.

    ```sh
    cd {project_directory}/backend
    npm install

    cd {project_directory}/frontend
    npm install
    ```

1. Configure the application environment variables in `{project_directory/backend/.env}`.

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
--base_dir=/home/yugabyte/yb_data --background=false

docker run -d --name yugabytedb_node2 --net custom-network \
-p 15434:15433 -p 7002:7000 -p 9002:9000 -p 5434:5433 \
-v ~/yb_docker_data/node2:/home/yugabyte/yb_data --restart unless-stopped \
yugabytedb/yugabyte:{{< yb-version version="preview" format="build">}} \
bin/yugabyted start --join=yugabytedb_node1 \
--base_dir=/home/yugabyte/yb_data --background=false

docker run -d --name yugabytedb_node3 --net custom-network \
-p 15435:15433 -p 7003:7000 -p 9003:9000 -p 5435:5433 \
-v ~/yb_docker_data/node3:/home/yugabyte/yb_data --restart unless-stopped \
yugabytedb/yugabyte:{{< yb-version version="preview" format="build">}} \
bin/yugabyted start --join=yugabytedb_node1 \
--base_dir=/home/yugabyte/yb_data --background=false
```

The database connectivity settings are provided in the `{project_dir}/backend/.env` file and do not need to be changed if you started the cluster with the command above.

Navigate to the YugabyteDB UI to confirm that the database is up and running, at <http://127.0.0.1:15433>.

![YugabyteDB Cluster Dashboard](/images/tutorials/azure/azure-openai/yb-cluster.png "YugabyteDB Cluster Dashboard")

## Load the Airbnb data set

As long as the application provides a lodging recommendation service for San Francisco, you can leverage a publicly available Airbnb data set with over 7500 relevant listings:

1. Copy the Airbnb schema and data to the first node's container:

    ```shell
    docker cp {project_dir}/sql/0_airbnb_listings.sql yugabytedb-node1:/home
    docker cp {project_dir}/sql/1_airbnb_embeddings.csv yugabytedb-node1:/home
   ```

1. Load the dataset to the cluster with properties in San Francisco.  Note: it can take a minute to load the data.:

    ```shell
    docker exec -it yugabytedb-node1 bin/ysqlsh -h yugabytedb-node1 -c '\i /home/0_airbnb_listings.sql'


    docker exec -it yugabytedb-node1 bin/ysqlsh -h yugabytedb-node1 \
    -c "\copy airbnb_listing from /home/sf_airbnb_listings.csv with DELIMITER ',' CSV HEADER"
   ```

1. Add the PostgreSQL `pgvector` extension and `description_embedding` column of type vector.

    ```shell
    docker exec -it yugabytedb-node1 bin/ysqlsh -h yugabytedb-node1 -c '\i /home/1_airbnb_embeddings.sql'
    ```

## Get started with Google Vertex AI

To start using Google Vertex AI in the application:

1. Create a project in Google Cloud.
1. Enable the Vertex AI API.
1. Install the [gcloud command-line utility](https://cloud.google.com/sdk/docs/install).
1. Log-in to Google Cloud via the CLI to run the application locally.

    ```shell
    gcloud auth application-default login
    ```

1. Update the Google Vertex AI environment variables in `{project_dir}/backend/.env`.

## Generate embeddings for Airbnb listings

Airbnb properties come with detailed descriptions of the accommodations, location, and various other features of the listing. By transforming the text in the `description` column of our database into a vectorized representation, we can use `pgvector` to execute a similarity search based on user prompts.

Execute the `embeddingsGenerator.js` script to generate embeddings in Google Vertex AI for each property, and store them in the `description_embedding` column in the database. This process can take over 10 minutes to complete.

```shell
node {project_directory}/backend/vertex/embeddingsGenerator.js
....
Processing rows starting from 34746755
Processed 7551 rows
Processing rows starting from 35291912
Finished generating embeddings for 7551 rows
```

## Run the pplication

1. Start the Node.js backend.

   ```
   cd {project_dir}/backend
   npm start
   ```

1. Start the React UI.

   ```
   npm run dev
   ```

1. Access the application UI at [http://localhost:5173](http://localhost:5173).

## Test the Application

Test the application with relevant prompts. For instance:

   *We're traveling to San Francisco from February 21st through 28th. We need a place to stay with parking available.*

   *I'm looking for an apartment near the Golden Gate Bridge with a nice view of the Bay.*

   *Full house with ocean views for a family of 6.*

   *Room for 1 in downtown SF, walking distance to Moscone Center.*

These prompts will first be sent to Vertex AI to be converted to embeddings. Next, the returned embeddings will be used to search for similar properties, stored in YugabyteDB.

```javascript
const dbRes = await pool.query(
    "SELECT name, description, price, 1 - (description_embedding <=> $1) as similarity " +
    "FROM airbnb_listing WHERE 1 - (description_embedding <=> $1) > 0.7 ORDER BY similarity DESC LIMIT 5",
    [embeddings]
   );
```

![YugaLodgings Application Search Results](/images/tutorials/google/google-vertex-ai/yugalodgings-search-results.png "YugaLodgings Application Search Results")

## Wrap-up

The Google Vertex AI service simplifies the process of designing, building, and productizing generative AI applications by offering developer APIs for various major programming languages.

With the help of the PostgreSQL pgvector extension, YugabyteDB enhances the scalability of these applications by distributing data and embeddings across a cluster of nodes, facilitating similarity searches on a large scale.

To learn how to run this application using Azure, see [Build scalable generative AI applications with Azure OpenAI and YugabyteDB](../../azure/azure-openai/).
