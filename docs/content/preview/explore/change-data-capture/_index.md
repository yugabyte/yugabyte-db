---
title: Change data capture (CDC)
headerTitle: Change data capture (CDC)
linkTitle: Change data capture
description: CDC or Change data capture is a process to capture changes made to data in the database.
headcontent: Capture changes made to data in the database
image: fa-light fa-rotate
cascade:
  earlyAccess: /preview/releases/versioning/#feature-maturity
menu:
  preview:
    identifier: explore-change-data-capture
    parent: explore
    weight: 280
type: indexpage
---
In databases, change data capture (CDC) is a set of software design patterns used to determine and track the data that has changed so that action can be taken using the changed data. CDC is beneficial in a number of scenarios:

- **Microservice-oriented architectures**: Some microservices require a stream of changes to the data, and using CDC in YugabyteDB can provide consumable data changes to CDC subscribers.

- **Asynchronous replication to remote systems**: Remote systems may subscribe to a stream of data changes and then transform and consume the changes. Maintaining separate database instances for transactional and reporting purposes can be used to manage workload performance.

- **Multiple data center strategies**: Maintaining multiple data centers enables enterprises to provide high availability (HA).

- **Compliance and auditing**: Auditing and compliance requirements can require you to use CDC to maintain records of data changes.

YugabyteDB supports the following methods for reading change events.

## PostgreSQL Replication Protocol

This method uses the [PostgreSQL replication protocol](using-logical-replication/key-concepts/#replication-protocols), ensuring compatibility with PostgreSQL CDC systems. Logical replication operates through a publish-subscribe model. It replicates data objects and their changes based on the replication identity.

It works as follows:

1. Create Publications in the YugabyteDB cluster similar to PostgreSQL.
1. Deploy the YugabyteDB Connector in your preferred Kafka Connect environment.
1. The connector uses replication slots to capture change events and publishes them directly to a Kafka topic.

{{<lead link="./using-logical-replication/">}}
To learn about CDC in YugabyteDB using the PostgreSQL Replication Protocol, see [CDC using PostgreSQL Replication Protocol](./using-logical-replication).
{{</lead>}}

## YugabyteDB gRPC Replication Protocol

This method involves setting up a change stream in YugabyteDB that uses the native gRPC replication protocol to publish change events.

It works as follows:

1. Establish a change stream in the YugabyteDB cluster using the yb_admin CLI commands.
1. Deploy the YugabyteDB gRPC Connector in your preferred Kafka Connect environment.
1. The connector captures change events using YugabyteDB's native gRPC replication and directly publishes them to a Kafka topic.

{{<lead link="./using-yugabytedb-grpc-replication/">}}
To learn about CDC in YugabyteDB using the gRPC Replication Protocol, see [CDC using gRPC Replication Protocol](./using-yugabytedb-grpc-replication/).
{{</lead>}}
