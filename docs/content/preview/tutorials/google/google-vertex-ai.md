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
In this tutorial, we'll walk you through the steps to build generative AI applications using Google Vertex AI and YugabyteDB.

The application provides lodging recommendations for travelers visiting San Francisco.

![YugaLodgings Application](/images/tutorials/google/google-vertex-ai/yugalodgings-main.png "YugaLodgings Application")

Using Google Vertex AI, text embeddings (a vectorized representation of the data) are generated for each listing description and stored in YugabyteDB, using the PostgreSQL `pgvector` extension. The user's prompt is similarly converted to text embeddings using Google Vertex AI and subsequently used to execute a similarity search in YugabyteDB, finding properties with descriptions related to the user's prompt.

## Prerequisites
* A Google Cloud account with appropriate permissions
* A YugabyteDB cluster of version [2.19.2 or later](https://download.yugabyte.com/)
* Install Node.js v18+
* Install [Docker](https://docs.docker.com/get-docker/)

## Initializing the Project

1. Clone the repository.
  ```shell
    git clone https://github.com/YugabyteDB-Samples/yugabytedb-google-vertexai-lodging-service.git
  ```
2. Install the application dependencies.
   ```shell
     cd {project_directory}/backend
     npm install


     cd {project_directory}/frontend
     npm install
   ```
3. Configure the application environment variables in `{project_directory/backend/.env}`.

## Start YugabyteDB and Load the Sample Dataset

1. Start a YugabyteDB isntance of version 2.19.2 or later:
```shell
mkdir ~/yb_docker_data

docker network create custom-network

docker run -d --name yugabytedb_node1 --net custom-network \
   -p 15433:15433 -p 7001:7000 -p 9001:9000 -p 5433:5433 \
   -v ~/yb_docker_data/node1:/home/yugabyte/yb_data --restart unless-stopped \
   yugabytedb/yugabyte:2.19.2.0-b121 \
   bin/yugabyted start \
   --base_dir=/home/yugabyte/yb_data --daemon=false

docker run -d --name yugabytedb_node2 --net custom-network \
   -p 15434:15433 -p 7002:7000 -p 9002:9000 -p 5434:5433 \
   -v ~/yb_docker_data/node2:/home/yugabyte/yb_data --restart unless-stopped \
   yugabytedb/yugabyte:2.19.2.0-b121 \
   bin/yugabyted start --join=yugabytedb_node1 \
   --base_dir=/home/yugabyte/yb_data --daemon=false
  
docker run -d --name yugabytedb_node3 --net custom-network \
   -p 15435:15433 -p 7003:7000 -p 9003:9000 -p 5435:5433 \
   -v ~/yb_docker_data/node3:/home/yugabyte/yb_data --restart unless-stopped \
   yugabytedb/yugabyte:2.19.2.0-b121 \
   bin/yugabyted start --join=yugabytedb_node1 \
   --base_dir=/home/yugabyte/yb_data --daemon=false
```
The database connectivity settings are provided in the `{project_dir}/backend/.env` file and do not need to be changed if you started the cluster with the command above.

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
## Getting Started with Google Vertex AI
To start using Google Vertex AI in the application:
1. Create a project in Google Cloud.
1. Enable the Vertex AI API.
1. Install the [gcloud command-line utility](https://cloud.google.com/sdk/docs/install).
1. Log-in to Google Cloud via the CLI to run the application locally.
  ```shell
  gcloud auth application-default login
  ```
1. Update the Google Vertex AI environment variables in `{project_dir}/backend/.env`.

## Generate Embeddings for Airbnb Listing Descriptions

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


## Running the Application

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


## Testing the Application


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

## Wrapping Up

Building generative AI applications using YugabyteDB is easily achieved with the help of PostgreSQL's pgvector extension and Google Vertex AI. If you're interested in more tutorials on the topic of generative AI, check out:

[Link to Azure OpenAI Tutorial]