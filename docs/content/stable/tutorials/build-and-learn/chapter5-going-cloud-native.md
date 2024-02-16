---
title: Offloading operations
headerTitle: "Chapter 5: Offloading operations with YugabyteDB Managed"
linkTitle: Offloading operations
description: Offloading management and maintenance of the database clusters with YugabyteDB Managed, fully managed databases-as-a-service
menu:
  stable:
    identifier: chapter5-offloading-operations
    parent: tutorials-build-and-learn
    weight: 6
type: docs
---

{{< note title="YugaPlus - Offloading operations" >}}
YugaPlus became one of the top streaming platforms with hundreds of millions people around the world spending countless hours watching their favorite movies, series and live events. The YugaPlus team mastered the art of scaling in the cloud by building the service that handles user traffic at low latency across multiple regions and tolerates all sorts of possible outages.

But the maintenance of such a platform was not a trivial task. The engineering and infrastructure teams spend a good chunk of their time just to keep the streaming platform running, secured and up-to-date.

Eventually, the YugaPlus team found a way how to spend more time on innovation. They transitioned to YugabyteDB Managed, a DBaaS that let them offload management, maintenance, and operations of their database cluster...
{{< /note >}}

In this chapter you'll learn:

* How deploy a free YugabyteDB Managed instance
* How to connect the application to the YugabyteDB Managed cluster

**Prerequisites**

You need to complete [chapter 4](../chapter4-going-global) of the tutorial before proceeding with this one.

{{< header Level="2" >}}Start YugabyteDB Managed Cluster{{< /header >}}

[YugabyteDB Managed](http://cloud.yugabyte.com/) is a fully managed YugabyteDB-as-a-Service that allows you to run YugabyteDB clusters on Amazon Web Services (AWS), Microsoft Azure, and Google Cloud Platform (GCP). The service offers a **free sandbox cluster** for everyone wishing to use YugabyteDB in a real cloud environment.

Follow these steps to deploy a free YugabyteDB Managed instance:

1. Create a YugabyteDB Managed account: <https://cloud.yugabyte.com/signup>

2. Begin creating the free cluster by clicking on the **Create a Free cluster** button.

    ![YugatebyDB Managed Create Cluster](/images/tutorials/build-and-learn/chapter5-create-free-cluster.png)

3. Choose the Sandbox cluster option:

    ![YugatebyDB Managed Sandbox Cluster](/images/tutorials/build-and-learn/chapter5-choose-sandbox.png)

4. Pick between available cloud providers and regions (note, Microsoft Azure is available only for paid clusters):

    ![YugatebyDB Managed Select Cloud](/images/tutorials/build-and-learn/chapter5-select-cloud.png)

5. Click on the **Add Current IP Address** button to add the address of your machine (where you run the YugaPlus application) to the IP Allow list:

    ![YugatebyDB Managed Add Address](/images/tutorials/build-and-learn/chapter5-add-your-address.png)

6. **Make sure to download** the file with your cluster credentials:

    ![YugatebyDB Managed Download Credentials](/images/tutorials/build-and-learn/chapter5-download-credentials.png)

7. Wait while the cluster is being created:

    ![YugatebyDB Managed Cluster Creation](/images/tutorials/build-and-learn/chapter5-cluster-creation-process.png)

It can take up to 5 minutes to spin up a sandbox instance and configure it for you.

{{< header Level="2" >}}Connect Application to YugabyteDB Managed{{< /header >}}

After the YugabyteDB Managed cluster is started, go ahead and connect the YugaPlus movies recommendation app to it.

First, rebuild the YugaPlus backend image to skip one of the files during the database migration phase:

1. Use `Ctrl+C` or run `{yugaplus-project-dir}/docker-compose stop` to stop the YugaPlus application containers.

2. Rename the `V2__create_geo_partitioned_user_library.sql` file to `skip_create_geo_partitioned_user_library.sql` making sure it's not applied during the database migration phase. The YugabyteDB Managed sandbox instance can't be used for the geo-partitioning use case that explored in chapter 4.

    ```shell
    cd {yugaplus-project-dir}/backend/src/main/resources/db/migration/
    mv V2__create_geo_partitioned_user_library.sql skip_create_geo_partitioned_user_library.sql
    ```

3. Navigate to the YugaPlus project dir:

    ```shell
    cd {yugaplus-project-dir}
    ```

4. Rebuild the Docker images:

    ```shell
    docker-compose build
    ```

Next, start the application connecting to your YugabyteDB Managed cluster:

1. Go to the YugabyteDB Managed **Settings** tab and copy the **public** address of your cluster instance:

    ![YugatebyDB Managed Public Address](/images/tutorials/build-and-learn/chapter5-public-address.png)

2. Open the`{yugaplus-project-dir}/docker-compose.yaml` file and update the following settings:

    ```yaml
    - DB_URL=jdbc:yugabytedb://${YOUR_YBM_PUBLIC_ADDRESS}:5433/yugabyte?sslmode=require
    - DB_USER=${YOUR_YBM_USER}
    - DB_PASSWORD=${YOUR_YBM_PASSWORD}
    ```

    * `${YOUR_YBM_PUBLIC_ADDRESS}` - is the public address of your YugabyteDB Managed instance.
    * `${YOUR_YBM_USER}` and `${YOUR_YBM_PASSWORD}` - your database credentials from the file that you downloaded during the cluster configuration.

3. Start the application:

    ```shell
    docker-compose up
    ```

As soon as the `yugaplus-backend` container gets started, it will apply the database migration files to your cloud database instance. You can see the created tables on the **Tables** tab of the YugabyteDB Managed dashboard:

![YugatebyDB Managed Tables](/images/tutorials/build-and-learn/chapter5-movie-tables.png)

{{< note title="Can't connect to YugabyteDB Managed?" >}}
In case the backend container fails to connect to YugabyteDB Managed, make sure that you've added your IP address to the [IP Allow list](https://docs.yugabyte.com/preview/yugabyte-cloud/cloud-secure-clusters/add-connections).
{{< /note >}}

{{< header Level="2" >}}Ask for Movie Recommendations One Last Time...{{< /header >}}

With the YugaPlus backend running and successfully connected to your YugabyteDB Managed cluster, let's do one last search for movie recommendations.

1. Go to [YugaPlus frontend UI](http://localhost:3000/)

2. Ask for movie recommendations:

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="#similarity-search" class="nav-link active" id="similarity-search-tab" data-toggle="tab"
       role="tab" aria-controls="similarity-search" aria-selected="true">
      <i class="fa-brands fa-apple" aria-hidden="true"></i>
      Similarity Search (OpenAI)
    </a>
  </li>
  <li>
    <a href="#full-text-search" class="nav-link" id="full-text-search-tab" data-toggle="tab"
       role="tab" aria-controls="full-text-search" aria-selected="false">
      <i class="fa-brands fa-linux" aria-hidden="true"></i>
      Full-text search
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="similarity-search" class="tab-pane fade show active" role="tabpanel" aria-labelledby="similarity-search-tab">
  {{% includeMarkdown "includes/chapter5-similarity-search.md" %}}
  </div>
  <div id="full-text-search" class="tab-pane fade" role="tabpanel" aria-labelledby="full-text-search-tab">
  {{% includeMarkdown "includes/chapter5-full-text-search.md" %}}
  </div>
</div>

And, as a one last hint, if it feels like that some queries are running slow, just go to the **Performance**->**Slow Queries** dashboard of YugabyteDB Managed to see if any of them requires some optimization:

![YugatebyDB Managed Slow Queries](/images/tutorials/build-and-learn/chapter5-slow-queries.png)

Congratulations you've completed chapter 5, the final chapter of the tutorial! Throughout the tutorial you learned essential capabilities of YugabyteDB that set you for the next stage of your development journey.

You started with PostgreSQL and then took advantage of YugabyteDB's feature and runtime compatibility with PostgreSQL by migrating to a distributed YugabyteDB cluster. Then, you learned how to tolerate various outages by deploying a multi-region YugabyteDB cluster and using the YugabyteDB Smart driver. After that, you practiced using the latency-optimized geo-partitioning design pattern to scale both reads and writes across various locations. In the end, you learned how to offload management and operations of your database cluster by migrating to YugabyteDB Manager.

Having said that, good luck building applications that scale and never fail!
