---
title: AWS MSK tutorial for YugabyteDB CDC
headerTitle: AWS MSK
linkTitle: AWS MSK
description: AWS MSK for Change Data Capture in YugabyteDB.
headcontent:
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
menu:
  preview:
    parent: cdc-tutorials
    identifier: cdc-aws-msk
    weight: 20
type: docs
---

## AWS MSK
# How to Integrate YugabyteDB with Amazon Redshift Database Using AWS Managed Kafka Stream and CDC Connector

  

In this tutorial, we’ll walk through how to integrate YugabyteDB with Amazon Redshift database using the Amazon Web Services(AWS) Managed Kafka Stream (MSK) and YugabyteDB Change Data Capture (CDC) connector.

## Introducing  Amazon Redshift and AWS MSK

Amazon Redshift is a fully managed cloud-based data warehouse service provided by [Amazon Web Services (AWS)](https://www.yugabyte.com/cloud/aws/). It is designed to handle petabyte-scale data warehousing and analytics workloads for businesses of all sizes.

Amazon Managed Streaming for Apache Kafka (AWS MSK) is a fully managed, highly available, and secure Apache Kafka service offered by Amazon Web Services (AWS). AWS MSK makes it easy for customers to build and run applications using Apache Kafka without having to manage and operate their own Kafka clusters.

## Integration of YugabyteDB (OLTP) with AWS Redshift (Data Warehouse)

YugabyteDB can seamlessly connect to Amazon Redshift using the [YugabyteDB CDC connector](https://docs.yugabyte.com/preview/architecture/docdb-replication/change-data-capture/) and AWS MSK. The seamless connection is achieved through the YugabyteDB CDC connector that helps users consume data from a data warehouse and an OLTP (transactional database) without additional processing or batch loads.
 
## YugabyteDB to Amazon Redshift Architecture

The diagram below (Figure 1) shows the end-to-end integration architecture of YugabyteDB to Amazon Redshift.

  
Figure 1 - End-to-End Architecture
 
  

The streamed change data from the YugabyteDB Debezium/Kafka connector is streamed directly to Amazon Redshift using AWS MSK.
|Data flow seq#  |  Operations/Tasks | Component Involved |
|--|--|--|
| 1 |YugabyteDB CDC Enabled and [Create the Stream ID](https://docs.yugabyte.com/preview/integrations/cdc/debezium/) for specific YSQL database (e.g. your db name) | YugabyteDB |
| 2 |[Download the YugabyteDB CDC Connector JAR](https://github.com/yugabyte/debezium-connector-yugabytedb/releases/download/v1.9.5.y.19/debezium-connector-yugabytedb-1.9.5.y.19.jar) | YugabyteDB Debezium Connector JAR |
| 3 |[Setup the AWS MSK Cluster](https://medium.com/@sharmaranupama/stream-data-from-yugabyte-cdc-to-aws-msk-using-debezium-a09490c54851) (refer to the IAM policies and roles)| AWS MSK, AWS IAM Roles & Policies
| 4 |Setup Amazon Redshift and keep the credentials ready for testing.|Amazon Redshift|
  

## How to Set Up Amazon Redshift Sink

1.  #### Install YugabyteDB
    

You have multiple options to [install or deploy YugabyteDB](https://docs.yugabyte.com/latest/deploy/) if you don't have one already available. Note: If you’re running a Windows Machine then you can [leverage Docker on Windows with YugabyteDB](https://docs.yugabyte.com/preview/quick-start/docker/).

2.  #### Install and Setup AWS MSK
    

Install and setup AWS MSK, using the article, [Stream Data From YugabyteDB CDC to AWS MSK Using Debezium](https://medium.com/@sharmaranupama/stream-data-from-yugabyte-cdc-to-aws-msk-using-debezium-a09490c54851). It helps configure IAM Policies and roles required for AWS MSK in AWS. Post creation of IAM Role and setup the MSK cluster using the same article.

3.  #### Create Cluster Configuration and Worker configuration in MSK

**Cluster Configuration:**
In AWS MSK, under MSK Clusters menu (on the left side) will show cluster configuration. Click to create a new configuration and enter the values below on the configuration settings. Then save it.

E.g. myclusterV2

*auto.create.topics.enable=true
default.replication.factor=3
min.insync.replicas=2
num.io.threads=8
num.network.threads=5
num.partitions=3
num.replica.fetchers=2
replica.lag.time.max.ms=30000
socket.receive.buffer.bytes=102400
socket.request.max.bytes=104857600
socket.send.buffer.bytes=102400
unclean.leader.election.enable=true
zookeeper.session.timeout.ms=18000*

Worker Configuration:

In AWS MSK, under “MSK Connect”, create a worker configuration (like below) and enter the properties if you are going to receive the data in JSON format.

*key.converter=org.apache.kafka.connect.json.JsonConverter
key.converter.schemas.enable=true
value.converter=org.apache.kafka.connect.json.JsonConverter
value.converter.schemas.enable=true*

 
NOTE: It is highly recommended to use schema registry with AVRO. In such a case, the above worker configuration is not required.

4.  #### Create MSK Connector for YugabyteDB
    
Create the Configuration of AWS MSK connector for YugabyteDB as referred in [point #6](https://medium.com/@sharmaranupama/stream-data-from-yugabyte-cdc-to-aws-msk-using-debezium-a09490c54851) and choose the worker configuration that you created in Step #3.

Use the values below in the connector configuration and change the yellow highlights with new data according to your specific YugabyteDB configuration.

*connector.class=io.debezium.connector.yugabytedb.YugabyteDBConnector
database.streamid=684d878b37e94b279454b0b8d6a2a305
database.user=yugabyte
database.dbname=yugabyte
tasks.max=1
time.precision.mode=connect
transforms=unwrap
database.server.name=dbserver12
database.port=5433
include.schema.changes=true
database.master.addresses=10.9.205.161:7100
key.converter.schemas.enable=true
database.hostname=10.9.205.161
database.password=xxxxx
transforms.unwrap.drop.tombstones=false
value.converter.schemas.enable=true
transforms.unwrap.type=io.debezium.connector.yugabytedb.transforms.YBExtractNewRecordState
table.include.list=public.target_cdctest
snapshot.mode=initial*

5.  #### Install Amazon Redshift


 
 6.  #### Create Custom Plugin for Amazon Redshift
    
[Download the Amazon Redshift Sink connector](https://www.confluent.io/connector/kafka-connect-aws-redshift/#download) and upload this zip file to your AWS MSK Custom Plugin. You can also rename it.
  
  7.  #### Create MSK Sink Connect for Amazon Redshift
    
[Create the Configuration of AWS MSK connector](https://medium.com/@sharmaranupama/stream-data-from-yugabyte-cdc-to-aws-msk-using-debezium-a09490c54851) (see point #6). ou will need to keep it with the Amazon Redshift configuration instead of the YugabyteDB configuration like below. (NOTE:Replace the values in yellow per your specific Amazon Redshift details and AWS MSK Kafka Topic name)

  
*connector.class=io.confluent.connect.aws.redshift.RedshiftSinkConnector
aws.redshift.port=5439
table.name.format=public.target_cdctest
confluent.topic.bootstrap.servers=b-4.bseetharamanmsk.acsf.c10.kafka.us-west-2.amazonaws.com:9092
tasks.max=1
topics=dbserver12.public.target_cdctest
aws.redshift.password=xxxxx
aws.redshift.domain=bseetharaman-redshift-demo-cluster.xxgs.us-west-2.redshift.amazonaws.com
aws.redshift.database=dev
delete.enabled=true
auto.evolve=true
confluent.topic.replication.factor=1
aws.redshift.user=awsuser
auto.create=false
insert.mode=insert
pk.mode=record_key
pk.fields=sno*

8.  #### Validate the Data in Amazon Redshift
  
Launch the Amazon Redshift query editor and query the tables that are synced from YugabyteDB.

## Conclusion and Summary

In this tutorial, we walked through step-by-step how to integrate YugabyteDB with Amazon Redshift using YugabyteDB’s CDC connector.

These steps help to integrate the YugabyteDB transactional database with your data warehouse (Amazon Redshift DB) so that it meets the requirements for your application. The metrics collected by YugabyteDB CDC provide an intuitive, visual way to monitor the health, performance, and progress of these CDC operations. For detailed information on CDC metrics, please refer to [Monitoring Change Data Capture (CDC) Using YugabyteDB’s Metrics Dashboard](https://www.yugabyte.com/blog/monitoring-cdc-using-metrics/).