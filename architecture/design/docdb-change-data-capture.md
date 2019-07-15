# Change Data Capture in YugaByte DB

**Change data capture** (or **CDC** for short) enables capturing changes performed to the data stored in YugaByte DB. This document provides an overview of the approach YugaByte DB uses for providing change capture stream on tables that can be consumed by third party applications. This feature is useful in a number of scenarios such as:

### Microservice-oriented architectures

There are some microservices that require a stream of changes to the data. For example, a search system powered by a service such as Elasticsearch may be used in conjunction with the database stores the transactions. The search system requires a stream of changes made to the data in YugaByte DB. 

### Asynchronous replication to remote systems

Remote systems such as caches and analytics pipelines may subscribe to the stream of changes, transform them and consume these changes.

### Two data center deployments

Two datacenter deployments in YugaByte DB leverage change data capture at the core.

> Note that in this design, the terms "data center", "cluster" and "universe" will be used interchangeably. We assume here that each YB universe is deployed in a single data-center.


# Setting up a CDC

To create a CDC stream, run the following commands on YSQL (or YCQL) APIs:
```
CREATE CDC
       FOR <namespace>.<table>
       INTO <target>
       WITH <options>;
```

Run the following command to drop an existing CDC stream:
```
DROP CDC FOR <namespace>.<table>;
```

Initially, `KAFKA` and `ELASTICSEARCH` will be the supported targets. The usage for each of these is shown below.

### Kafka as the CDC target

```
CREATE CDC FOR my_database.my_table
           INTO KAFKA
           WITH cluster_address = 'http://localhost:9092',
                topic = 'my_table_topic',
                record = AFTER;
```

The CDC options for Kafka include:

| Option name       | Default       | Description   |
| ----------------- | ------------- | ------------- |
| `cluster_address` | -             | The `host:port` of the Kafka target cluster |
| `topic`           | Table name    | The Kafka topic to which the stream is published |
| `record`          | AFTER         | The type of records in the stream. Valid values are `CHANGE` (changed values only), `AFTER` (the entire row after the change is applied), ALL (the before and after value of the row). |

### Elasticsearch as the CDC target

```
CREATE CDC FOR my_database.my_table
           INTO ELASTICSEARCH
           WITH cluster_address = 'http://localhost:9200',
                index = 'my_table_index';
```

The CDC options for Elasticsearch include:

| Option name       | Default       | Description   |
| ----------------- | ------------- | ------------- |
| `cluster_address` | -             | The `host:port` of the Elasticsearch target cluster |
| `index`           | Table name    | The Elasticsearch index into which the search index is built |


# Design

### Process Architecture


```
                          ╔═══════════════════════════════════════════╗
                          ║  Node #1                                  ║
                          ║  ╔════════════════╗ ╔══════════════════╗  ║
                          ║  ║    YB-Master   ║ ║    YB-TServer    ║  ║  CDC Service is stateless
    CDC Streams metadata  ║  ║  (Stores CDC   ║ ║  ╔═════════════╗ ║  ║           |
    replicated with Raft  ║  ║   metadata)    ║ ║  ║ CDC Service ║ ║  ║<----------'
             .----------->║  ║                ║ ║  ╚═════════════╝ ║  ║
             |            ║  ╚════════════════╝ ╚══════════════════╝  ║
             |            ╚═══════════════════════════════════════════╝
             |                 
             |
             |_______________________________________________.
             |                                               |
             V                                               V
  ╔═══════════════════════════════════════════╗    ╔═══════════════════════════════════════════╗
  ║  Node #2                                  ║    ║  Node #3                                  ║
  ║  ╔════════════════╗ ╔══════════════════╗  ║    ║  ╔════════════════╗ ╔══════════════════╗  ║
  ║  ║    YB-Master   ║ ║    YB-TServer    ║  ║    ║  ║    YB-Master   ║ ║    YB-TServer    ║  ║
  ║  ║  (Stores CDC   ║ ║  ╔═════════════╗ ║  ║    ║  ║  (Stores CDC   ║ ║  ╔═════════════╗ ║  ║
  ║  ║   metadata)    ║ ║  ║ CDC Service ║ ║  ║    ║  ║   metadata)    ║ ║  ║ CDC Service ║ ║  ║
  ║  ║                ║ ║  ╚═════════════╝ ║  ║    ║  ║                ║ ║  ╚═════════════╝ ║  ║
  ║  ╚════════════════╝ ╚══════════════════╝  ║    ║  ╚════════════════╝ ╚══════════════════╝  ║
  ╚═══════════════════════════════════════════╝    ╚═══════════════════════════════════════════╝

```
