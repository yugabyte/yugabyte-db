---
title: Apache Atlas
linkTitle: Apache Atlas
description: Use Apache Atlas with YCQL API
menu:
  preview_integrations:
    identifier: ycql-atlas
    parent: integrations-other
    weight: 571
type: docs
---
[Apache Atlas](https://atlas.apache.org/) is an enterprise-scale open data management service which provides governance for Hadoop and the entire enterprise data ecosystem.

This tutorial describes how to set up Apache Atlas with YugabyteDB and run the quick start provided by the Atlas service.

## Prerequisites

To use [Apache Atlas](https://doc.akka.io/docs/akka-persistence-r2dbc/current/overview.html), ensure that you have the following:

- YugabyteDB up and running. Download and install YugabyteDB by following the steps in [Quick start](../../quick-start/).

- [Apache Solr 5.5.1](https://solr.apache.org/guide/6_6/installing-solr.html) installed. [Solr](https://solr.apache.org/) is an open-source indexing platform that serves as an indexing backend to run Apache Atlas.

## Build the Apache Atlas Project

To get the Apache Atlas server file, you need to build the Apache Atlas source using the following steps:

1. Clone the [source](https://github.com/apache/atlas) from GitHub to your local setup. Checkout to the latest stable release tag (for example, release-2.3.0) and follow the steps in the README to build the files.

1. After the source files are packaged, the Atlas server tar should be available in the `distro/target` folder.

1. Unzip the tar file using the following command:

    ```sh
    tar -xzvf apache-atlas-2.3.0-server.tar.gz
    ```

## Run Apache Atlas

Perform the following steps to run the Atlas server:

1. Start Solr in SolrCloud mode. Refer to [Getting started with SolrCloud](https://solr.apache.org/guide/6_6/getting-started-with-solrcloud.html). When prompted for the number of nodes, enter `1` and choose the default options for the other questions.

1. After SolrCloud is started, create a few configuration sets using the following commands from the Solr home directory.

    ```sh
    bin/solr create -c vertex_index -shards 2 \ -replicationFactor 2
    bin/solr create -c edge_index -shards 2 \ -replicationFactor 2
    bin/solr create -c fulltext_index -shards 2 \ -replicationFactor 2
    ```

1. From the unzipped Atlas home directory, modify the configurations in `conf/atlas-application.properties` file to use YugabyteDB YCQL as the graph backend as follows:

    ```conf
    # Graph storage
    atlas.graph.storage.backend=cql
    atlas.graph.storage.username=cassandra
    atlas.graph.storage.password=cassandra
    atlas.graph.storage.hostname=localhost
    atlas.graph.storage.cassandra.keyspace=JanusGraph
    atlas.graph.storage.clustername=cassandra
    atlas.graph.storage.port=9042
    atlas.EntityAuditRepository.impl=org.apache.atlas.repository.audit.CassandraBasedAuditRepository

    #Comment the following Hbase specific storage properties properties
    #Hbase
    #atlas.graph.storage.hostname=localhost
    #atlas.graph.storage.hbase.regions-per-server=1
    ```

1. Change the `atlas.graph.index.search.solr.zookeeper-url` in the `conf/atlas-application.properties` file to point to ZooKeeper started by Solr. The default value is `localhost:9983`. An example Solr URL is as follows:

    ```conf
    #Solr
    #Solr cloud mode properties
    atlas.graph.index.search.solr.zookeeper-url=localhost:9983
    ```

1. Start the Atlas Server from the Atlas home directory as follows:

    ```sh
    bin/atlas_start.py
    ```

    You should see the following output:

    ```output
    Starting Atlas server on host: localhost
    Starting Atlas server on port: 21000
    .........................
    Apache Atlas Server started!!!
    ```

1. To ensure the server has started successfully, run the following command:

    ```sh
    # The default username and password for atlas is admin
    curl -u username:password http://localhost:21000/api/atlas/admin/version
    ```

    You should see the following output:

    ```output
    {"Description":"Metadata Management and Data Governance Platform over Hadoop",    "Revision":"4cd215e1e2a04acbcd8afe6af95f43c4979202f1","Version":"2.3.0","Name":"apache-atlas"}
    ```

1. Run the quick start script using the following command:

    ```sh
    bin/quick_start.py
    ```

    When prompted for a username and password, enter `admin`. After the script completes, you should see the following output:

    ```output
    Creating sample types:
    Created type [DB]
    Created type [Table]
    .
    Created type [Table_Columns]
    Created type [Table_StorageDesc]

    Creating sample entities:
    Created entity of type [DB], guid: f87936f1-d620-4f70-88c1-471f30e95c68
    .
    Created entity of type [LoadProcess], guid: c4c0e468-5af9-474f-acc1-088747ebf199
    Created entity of type [LoadProcessExecution], guid: a03dd462-437d-42ee-b30f-dd0b8f1e9413
    Created entity of type [LoadProcessExecution], guid: 2fe2e4c2-04ac-4c34-9a14-67436428949d

    Sample DSL Queries:
    query [from DB] returned [3] rows.
    query [DB] returned [3] rows.
    .
    query [from DataSet] returned [10] rows.
    query [from Process] returned [3] rows.

    Sample Lineage Info:
    loadSalesMonthly(LoadProcess) -> sales_fact_monthly_mv(Table)
    time_dim(Table) -> loadSalesDaily(LoadProcess)
    sales_fact_daily_mv(Table) -> loadSalesMonthly(LoadProcess)
    sales_fact(Table) -> loadSalesDaily(LoadProcess)
    loadSalesDaily(LoadProcess) -> sales_fact_daily_mv(Table)
    Sample data added to Apache Atlas Server.
    ```

1. You can verify that the `janusgraph` and `atlas_audit` keyspaces were created using the [ycqlsh](../../admin/ycqlsh/#starting-ycqlsh) shell as follows:

    ```sql
    ycqlsh> DESC KEYSPACES;
    ```

    You should see the following output:

    ```output
    atlas_audit  system_auth  system_schema  janusgraph  system
    ```

## Clean up

You can stop the Atlas server using the following command:

  ```sh
  bin/atlas_stop.py
  ```

You can stop SolrCloud using the following command:

  ```sh
  bin/solr stop -all
  ```
