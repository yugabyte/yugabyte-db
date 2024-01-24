---
title: Debuting with PostgreSQL
headerTitle: "Chapter 1: Debuting with PostgreSQL"
linkTitle: Debuting with PostgreSQL
description: Build and launch the first version of the YugaPlus streaming service on PostgreSQL.
menu:
  stable:
    identifier: chapter1-debuting-with-postgres
    parent: tutorials-build-and-learn
    weight: 2
type: docs
---

{{< note title="YugaPlus - The Story Begins" >}}
The first version of the YugaPlus streaming service was set for its prime time debut. The plan involved initially launching the service for users on the US West Coast, followed by a gradual rollout to users nationwide.

The launch was successful, with the first user signing up for YugaPlus to enjoy their favorite movies on demand. The service, powered by PostgreSQL, handled the incoming traffic with ease.
{{< /note >}}

In this chapter, you'll deploy one of the YugaPlus microservices responsible for searching the movie catalog and managing the user library.

You'll learn the following:

* How to deploy PostgreSQL with pgvector in Docker.
* How to perform vector similarity searches with pgvector and the OpenAI Embedding model.

**Prerequisites**

* [Docker](https://www.docker.com) 20 or later.
* An [OpenAI API key](https://platform.openai.com/docs/overview). Without the API key, the microservice will perform full-text searches over the movie catalog instead of vector similarity searches. Note that the full-text search version is much less advanced.

{{< header Level="2" >}}Start PostgreSQL With pgvector{{< /header >}}

The pgvector extension transforms PostgreSQL into a vector database capable of storing and accessing vectorized data. The movie catalog microservice utilizes pgvector to provide users with highly relevant movie recommendations based on their input.

Follow these steps to start a PostgreSQL instance with pgvector and enable the extension:

1. Create a directory to serve as the volume for the PostgreSQL container:

    ```shell
    mkdir ~/postgres-volume
    ```

2. Create a custom Docker network that will be used by PostgreSQL and other containers throughout this tutorial:

    ```shell
    docker network create yugaplus-network
    ```

3. Start a PostgreSQL container using the latest version of the image with pgvector:

    ```shell
    docker run --name postgres --net yugaplus-network \
        -e POSTGRES_USER=postgres -e POSTGRES_PASSWORD=password \
        -p 5432:5432 \
        -v ~/postgresql-volume/:/var/lib/postgresql/data \
        -d ankane/pgvector:latest
    ```

4. Check the logs to ensure the container is up and running and PostgreSQL has initialized successfully:

    ```shell
    docker container ls -f name=postgres
    ```

5. Connect to the container and enable the pgvector extension:

    ```shell
    docker exec -it postgres-pgvector psql -U postgres -c 'CREATE EXTENSION vector'
    ```

With the database operational, you're now ready to deploy the first version of YugaPlus on your machine!

{{< header Level="2" >}}Deploy YugaPlus Microservice{{< /header >}}

The microservice consists of a React frontend and a Java backend. You don't need prior knowledge of React or Java, nor do you have to install any language-specific toolchains. Both the frontend and backend are deployed in Docker, which automatically pulls all required libraries and frameworks.

**Prepare for the deployment:**

1. Clone the YugaPlus repository:

    ```shell
    git clone https://github.com/YugabyteDB-Samples/YugaPlus.git .
    ```

2. Create an OpenAI API key: <https://platform.openai.com>

{{< tip title="Free Tier">}}
At the time of writing, OpenAI offered a generous free tier, which was more than sufficient to complete this tutorial.
{{< /tip >}}

**Start the backend:**

1. Navigate to the backend directory of the project:

    ```shell
    cd {yugaplus-dir}/backend
    ```

2. Build the Docker image:

    ```shell
    docker build -t yugaplus-backend .
    ```

3. Start a backend container using your `OPENAI_KEY`:

    ```shell
    docker run --name yugaplus-backend --net yugaplus-network -p 8080:8080 \
        -e DB_URL=jdbc:postgresql://postgres:5432/postgres \
        -e DB_USER=postgres \
        -e DB_PASSWORD=password \
        -e OPENAI_KEY=sk... \
        yugaplus-backend
    ```

The backend connects to the PostgreSQL container, initializes the movies catalog, and pre-loads a sample dataset with over 2,800 movies. This dataset includes embeddings pre-generated for movie overviews using the OpenAI Embedding model (`text-embedding-ada-002`). Upon successful startup, the backend will display the following messages in the terminal window:

```output
INFO 1 --- [main] o.s.b.w.embedded.tomcat.TomcatWebServer  : Tomcat started on port 8080 (http) with context path ''
INFO 1 --- [main] c.y.backend.YugaPlusBackendApplication   : Started YugaPlusBackendApplication in 18.681 seconds (process running for 19.173)
```

**Start the frontend:**

1. Open another terminal window and navigate to the frontend directory of the project:

    ```shell
    cd {yugaplus-dir}/frontend
    ```

2. Build the Docker image:

    ```shell
    docker build -t yugaplus-frontend . 
    ```

3. Deploy a frontend container:

    ```shell
    docker run --name yugaplus-frontend --net yugaplus-network -p 3000:3000 \
        -e REACT_APP_PROXY_URL=http://yugaplus-backend:8080 \
        yugaplus-frontend
    ```

The frontend container starts in a few seconds and is accessible at the following address: <http://localhost:3000/>

{{< header Level="2" >}}Search For Your Favorite Movies{{< /header >}}

TBD:

* Quickly explain the schema (movie and user history table)
* Explain the overview and overview_vector columns
* Suggest to search for a movie (a tab for the 'AI search' and 'Full Text Search')
* Suggest to pick different categories and add movies to the catalogu
* Show sql queries that are executed by AI and Full Text search (also in separate tabs. just use the backend to print the generated query)

Congratulations!!!
