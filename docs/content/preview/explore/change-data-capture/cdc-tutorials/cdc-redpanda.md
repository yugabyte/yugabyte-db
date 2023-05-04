---
title: Redpanda tutorial for YugabyteDB CDC
headerTitle: Redpanda
linkTitle: Redpanda
description: Redpanda for Change Data Capture in YugabyteDB.
headcontent:
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
menu:
  preview:
    parent: cdc-tutorials
    identifier: cdc-redpanda
    weight: 40
type: docs
---

## Redpanda
# YugabyteDB CDC using Redpanda as Message Broker

  

In this tutorial, we’ll walk through how to integrate YugabyteDB CDC Connector with Redpanda.

## Introducing YugabyteDB, Redpanda

  

[YugabyteDB](https://www.yugabyte.com/yugabytedb/) is an open-source [distributed SQL database](https://www.yugabyte.com/distributed-sql/distributed-sql-database/) for transactional (OLTP) applications. YugabyteDB is designed to be a powerful cloud-native database that can [run in any cloud – private, public, or hybrid](https://www.yugabyte.com/compare-products/).

  

[Redpanda](https://redpanda.com/) is an open-source distributed streaming platform designed to be fast, scalable, and efficient. It is built using modern technologies like Rust, and is designed to handle large volumes of data in real-time. Redpanda is similar to other messaging systems like Apache Kafka, but with some key differences. For example, Redpanda provides better performance, lower latency, and improved resource utilization compared to Kafka

  

Here are some of the key differences between Redpanda and Kafka:

1.  Performance: Redpanda is designed for high-performance and low-latency, with a focus on optimizing performance for modern hardware. Redpanda uses a zero-copy design, which eliminates the need to copy data between kernel and user space, resulting in faster and more efficient data transfer.
    
2.  Scalability: Redpanda is designed to scale horizontally and vertically with ease, making it easy to add or remove nodes from a cluster. It also supports multi-tenancy, which allows multiple applications to share the same cluster.
    
3.  Storage: Redpanda stores data in a columnar format, which allows for more efficient storage and faster query times. This also enables Redpanda to support SQL-like queries on streaming data.
    
4.  Security: Redpanda includes built-in security features such as TLS encryption and authentication, while Kafka relies on third-party tools for security.
    
5.  API compatibility: Redpanda provides an API that is compatible with the Kafka API, making it easy to migrate from Kafka to Redpanda without having to change existing applications.
    

## YugabyteDB CDC using Redpanda Architecture

The diagram below (Figure 1) shows the end-to-end integration architecture of YugabyteDB CDC using Redpanda

  

Figure 1 - End to End Architecture

  

Below table shows the data flow sequences with their operations and tasks performed.
|Data flow seq#  |  Operations/Tasks | Component Involved |
|--|--|--|
| 1 |CDC Enabled and [Create the Stream ID](https://docs.yugabyte.com/preview/integrations/cdc/debezium/) for specific YSQL database (e.g. your db name)   | YugabyteDB |
| 2 |Install and configure Redpanda as per this [document](https://docs.redpanda.com/docs/get-started/quick-start/?quickstart=docker) and download YugabyteDB Debezium Connector as referred in [point#3](https://docs.google.com/document/d/1b2dQfMydXWr1iQ7SY_-l0Gda9NdklrHW-a6kBAoUKhg/edit#heading=h.earrcamsknhe) | Redpanda Cloud or Redpanda Docker and YugabyteDB CDC Connector |
| 3 |Create and Deploy connector configuration in Redpanda as mentioned in step #4 | Redpanda, Kafka Connect 

## How to Set Up Redpanda with YugabyteDB CDC

1.  #### Install YugabyteDB
    

You have multiple options to [install or deploy YugabyteDB](https://docs.yugabyte.com/latest/deploy/) if you don't have one already available. Note: If you’re running a Windows Machine then you can [leverage Docker on Windows with YugabyteDB](https://docs.yugabyte.com/preview/quick-start/docker/).

2.  #### Install and Setup Redpanda
    

Using this Redpanda document [link](https://docs.redpanda.com/docs/get-started/quick-start/?quickstart=docker), helps to spin up the Redpanda cluster using single broker configuration or multi-broker configuration using docker-compose or using a Redpanda cloud account.

Post installation and setup using the docker option, we can see below docker containers are up and running. It shows two docker containers (redpanda-console and redpanda broker) in the below screenshot (Figure 2)

Figure 2 - Redpanda Docker Containers

    

3.  #### Deploy YugabyteDB Debezium connector (docker container):
    

#### Link the Redpanda Broker Address with YugabyteDB CDC Connector as highlighted in yellow below

sudo docker run -it --rm --name connect --net=host -p 8089:8089 -e GROUP_ID=1 -e BOOTSTRAP_SERVERS=127.0.0.1:19092 -e CONNECT_REST_PORT=8082 -e CONNECT_GROUP_ID="1" -e CONFIG_STORAGE_TOPIC=my_connect_configs -e OFFSET_STORAGE_TOPIC=my_connect_offsets -e STATUS_STORAGE_TOPIC=my_connect_statuses -e CONNECT_KEY_CONVERTER="org.apache.kafka.connect.json.JsonConverter" -e CONNECT_VALUE_CONVERTER="org.apache.kafka.connect.json.JsonConverter" -e CONNECT_INTERNAL_KEY_CONVERTER="org.apache.kafka.connect.json.JsonConverter" -e CONNECT_INTERNAL_VALUE_CONVERTER="org.apache.kafka.connect.json.JsonConverter" -e CONNECT_REST_ADVERTISED_HOST_NAME="connect" quay.io/yugabyte/debezium-connector:latest
  
The below diagram(Figure 3) show 3 docker containers including YugabyteDB Debezium connector and Redpanda connectors

Figure 3 - Redpanda Docker Containers

4.  #### Deploy the source connector using Redpanda

Source Connector: Create and deploy the source connector as mentioned below, change the database hostname, database master addresses, database user, password, database name, logical server name and table include list and StreamID as per your configuration (in yellow)

curl -i -X  POST -H  "Accept:application/json" -H  "Content-Type:application/json" localhost:8083/connectors/ -d '{
"name": "srcdb",
"config": {
"connector.class": "io.debezium.connector.yugabytedb.YugabyteDBConnector",
"database.hostname":"10.9.205.161",
"database.port":"5433",
"database.master.addresses": "10.9.205.161:7100",
"database.user": "yugabyte",
"database.password": "xxxx",
"database.dbname" : "testcdc",
"database.server.name": "dbeserver5",
"table.include.list":"public.balaredpandatest",
"database.streamid":"d36ef18084ed4ad3989dfbb193dd2546",
"snapshot.mode":"initial",
"transforms": "unwrap",
"transforms.unwrap.type": "io.debezium.connector.yugabytedb.transforms.YBExtractNewRecordState",
"transforms.unwrap.drop.tombstones": "false",
"time.precision.mode": "connect",
"key.converter":"io.confluent.connect.avro.AvroConverter",
"key.converter.schema.registry.url":"http://localhost:18081",
"key.converter.enhanced.avro.schema.support":"true",
"value.converter":"io.confluent.connect.avro.AvroConverter",
"value.converter.schema.registry.url":"http://localhost:18081",
"value.converter.enhanced.avro.schema.support":"true"
	}
}'

  5.  #### Monitor the Messages through Redpanda

Below diagram shows the Redpanda broker details that we installed locally using docker and the topic that we subscribed (e.g. dbeserver5.public.balaredpandatest) and the schema registry with key and value details of the topic.

## Conclusion and Summary

In this tutorial, we walked through step-by-step how to integrate YugabyteDB Change Data Capture with Redpanda. It helps to connect different sinks that are compatible with Redpanda.