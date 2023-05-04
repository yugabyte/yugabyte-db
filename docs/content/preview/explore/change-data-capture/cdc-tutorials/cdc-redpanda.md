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


YugabyteDB CDC using Redpanda as Message Broker
In this blog, we’ll walk through how to integrate YugabyteDB CDC Connector with Redpanda.

Introducing YugabyteDB, Redpanda
YugabyteDB is an open-source distributed SQL database for transactional (OLTP) applications. YugabyteDB is designed to be a powerful cloud-native database that can run in any cloud – private, public, or hybrid.

Redpanda is an open-source distributed streaming platform designed to be fast, scalable, and efficient. It is built using modern technologies like Rust, and is designed to handle large volumes of data in real-time. Redpanda is similar to other messaging systems like Apache Kafka, but with some key differences. For example, Redpanda provides better performance, lower latency, and improved resource utilization compared to Kafka

Here are some of the key differences between Redpanda and Kafka:

Performance: Redpanda is designed for high-performance and low-latency, with a focus on optimizing performance for modern hardware. Redpanda uses a zero-copy design, which eliminates the need to copy data between kernel and user space, resulting in faster and more efficient data transfer.
Scalability: Redpanda is designed to scale horizontally and vertically with ease, making it easy to add or remove nodes from a cluster. It also supports multi-tenancy, which allows multiple applications to share the same cluster.
Storage: Redpanda stores data in a columnar format, which allows for more efficient storage and faster query times. This also enables Redpanda to support SQL-like queries on streaming data.
Security: Redpanda includes built-in security features such as TLS encryption and authentication, while Kafka relies on third-party tools for security.
API compatibility: Redpanda provides an API that is compatible with the Kafka API, making it easy to migrate from Kafka to Redpanda without having to change existing applications.
YugabyteDB CDC using Redpanda Architecture
The diagram below shows the end-to-end integration architecture of YugabyteDB CDC using Redpanda:

End-to-End Architecture

The following table shows the data flow sequences with their operations and tasks performed:

Data flow seq#	Operations/Tasks	Component Involved
1	YugabyteDB CDC Enabled and Create the Stream ID for specific YSQL database (e.g. your db name)	YugabyteDB
2	Install and configure Redpanda as per this document and download YugabyteDB Debezium Connector as referred in point#3	Redpanda Cloud or Redpanda Docker and YugabyteDB CDC Connector
3	Create and Deploy connector configuration in Redpanda as mentioned in section#	Redpanda, Kafka Connect
How to Set Up Redpanda with YugabyteDB CDC
Install YugabyteDB
You have multiple options to install or deploy YugabyteDB if you don't have one already available. Note: If you’re running a Windows Machine then you can leverage Docker on Windows with YugabyteDB.

Install and Setup Redpanda
Using this Redpanda document link, helps to spin up the Redpanda cluster using single broker configuration or multi-broker configuration using docker-compose or using a Redpanda cloud account.

Post installation and setup using the docker option, we can see below docker containers are up and running. It shows two docker containers (redpanda-console and redpanda broker) in the below screenshot: