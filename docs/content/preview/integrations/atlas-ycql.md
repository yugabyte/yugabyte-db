---
title: Apache Atlas with YCQL API
linkTitle: Apache Atlas
description: Use Apache Atlas with YCQL API
menu:
  preview_integrations:
    identifier: ycql-atlas
    parent: integrations
    weight: 572
type: docs
---

This tutorial describes how to set up Apache Atlas to work with YugabyteDB and run the quick start provided by it.

## Prerequisites

To use the [Apache Atlas](https://doc.akka.io/docs/akka-persistence-r2dbc/current/overview.html), ensure that you have the following:

- YugabyteDB up and running. Download and install YugabyteDB by following the steps in [Quick start](../../quick-start/).

- [Apache Solr](https://solr.apache.org/guide/6_6/installing-solr.html) is installed.

## Build The Apache Atlas Project

To get the Apache Atlas server file, you need to build the Apache Atlas source first. To do that:

1. Clone the [source](https://github.com/apache/atlas) from github to your local setup. The latest stable release is 2.3.0, so checkout to the tag: release-2.3.0 and follow the steps in the README.

1. Once packaged, you will see the Apache Atlas server tar in the folder distro/target.

1. Unzip the tar file by running the command:

    ```sh
    tar -xzvf apache-atlas-2.3.0-server.tar.gz
    ```

## Run Apache Atlas With YugabyteDB

First, we need to start Solr in SolrCloud mode. To do this, refer to Getting started with SolrCloud.
Once SolrCloud is started, we need to create a few config sets. To do this, run the following commands from the  <solr-installed-dir>:

```sh
bin/solr create -c vertex_index -shards 2 \ -replicationFactor 2
bin/solr create -c edge_index -shards 2 \ -replicationFactor 2
bin/solr create -c fulltext_index -shards 2 \ -replicationFactor 2
```

In the unzipped Atlas server folder, you will find a conf/atlas-application.properties file. To use YugabyteDB YCQL as the graph backend, use the following configurations in the file:

```conf
atlas.graph.storage.backend=cql
atlas.graph.storage.username=cassandra
atlas.graph.storage.password=cassandra
atlas.graph.storage.cassandra.keyspace=JanusGraph
#In order to use Cassandra as a backend, comment out the hbase specific properties above, and uncomment the
#the following properties
atlas.graph.storage.clustername=cassandra
atlas.graph.storage.port=9042
atlas.EntityAuditRepository.impl=org.apache.atlas.repository.audit.CassandraBasedAuditRepository
```

Once the backend is configured, also change the atlas.graph.index.search.solr.zookeeper-url in the conf file to point to the zookeeper started by Solr. The default value for this is: localhost:9983
Now the configuration is complete. We can start the Atlas Server. To do this, run the command from the <Atlas-Server-Dir>:
bin/atlas_start.py
You should see the following output:
Starting Atlas server on host: localhost
Starting Atlas server on port: 21000
.........................
Apache Atlas Server started!!!
To ensure the server has started successfully, run the following command:
curl -u username:password http://localhost:21000/api/atlas/admin/version
The default username and password for atlas is admin.
You should see the following output:
{"Description":"Metadata Management and Data Governance Platform over Hadoop","Revision":"4cd215e1e2a04acbcd8afe6af95f43c4979202f1","Version":"2.3.0","Name":"apache-atlas"}
Run the quick start script after starting the server. To do this, run the following command:
bin/quick_start.py
A prompt asking for username and password for atlas comes up. Put admin for both. Once done, you should see the following output:


Creating sample types:
Created type [DB]
Created type [Table]
.
.
.
Created type [Table_Columns]
Created type [Table_StorageDesc]

Creating sample entities:
Created entity of type [DB], guid: f87936f1-d620-4f70-88c1-471f30e95c68
.
.
.
Created entity of type [LoadProcess], guid: c4c0e468-5af9-474f-acc1-088747ebf199
Created entity of type [LoadProcessExecution], guid: a03dd462-437d-42ee-b30f-dd0b8f1e9413
Created entity of type [LoadProcessExecution], guid: 2fe2e4c2-04ac-4c34-9a14-67436428949d

Sample DSL Queries:
query [from DB] returned [3] rows.
query [DB] returned [3] rows.
.
.
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

You can also verify the entities created in the ycqlsh shell.
To stop the Atlas server, run the following command:
bin/atlas_stop.py


