# Change Data Capture in YugabyteDB

**Change data capture** (or **CDC** for short) enables capturing changes performed to the data stored in YugabyteDB. This document provides an overview of the approach YugabyteDB uses for providing change capture stream on tables that can be consumed by third party applications. This feature is useful in a number of scenarios such as:

### Microservice-oriented architectures

There are some microservices that require a stream of changes to the data. For example, a search system powered by a service such as Elasticsearch may be used in conjunction with the database stores the transactions. The search system requires a stream of changes made to the data in YugabyteDB. 

### Asynchronous replication to remote systems

Remote systems such as caches and analytics pipelines may subscribe to the stream of changes, transform them and consume these changes.

### Two data center deployments

Two datacenter deployments in YugabyteDB leverage change data capture at the core.

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

Creating a new CDC stream on a table returns a stream UUID. The CDC Service stores information about all streams in the system table `cdc_streams`. The schema for this table looks as follows:

```
cdc_streams {
stream_id	text,
params	map<text, text>,
primary key (stream_id)
}
```

Along with creating a CDC stream, a CDC subscriber is also created for all existing tablets of the stream. A new subscriber entry is created in the `cdc_subscribers` table. The schema for this table is:
```
cdc_subscribers {
stream_id		text,
subscriber_id	text,
tablet_id		text,
data			map<text, text>,
primary key (stream_id, subscriber_id, tablet_id)
}
```

Every YB-TServer has a `CDC service` that is stateless. The main APIs provided by `CDC Service` are:
* `SetupCDC` API for setting up CDC stream on a table.
* `RegisterSubscriber` API for registering a subscriber that will read changes from some or all tablets for that CDC stream.
* `GetChanges` API that will be used by subscriber to get latest set of changes.
* `GetSnapshot` API that will be used to bootstrap subscribers and get current snapshot of the database (typically will be invoked prior to GetChanges)

### Pushing changes to external systems

Each tserver has `cdc_subscribers` that are responsible for getting changes for all tablets for which the tserver is a leader. When a new stream and subscriber are created, the tserver `cdc_subscribers` detect this and start invoking the `cdc_service.GetChanges` API periodically to get the latest set of changes.

While invoking `GetChanges`, the cdc subscriber needs to pass in a `from_checkpoint` which is the last OP ID that it successfully consumed. When CDC service receives a request of `GetChanges` for a tablet, it reads the changes from WAL (log cache) starting from from_checkpoint, deserializes them and returns those to CDC subscriber. It also records the `from_checkpoint` in `cdc_subscribers` table in the data column. This will be used for bootstrapping fallen subscribers who don’t know the last checkpoint or in case of tablet leader changes.

When `cdc_subscribers` receive the set of changes, they then push these changes out to Kafka or Elastic Search.


### CDC Guarantees

#### Per-tablet ordered delivery guarantee
All changes for a row (or rows in the same tablet) will be received in the order in which they happened. However, due to the distributed nature of the problem, there is no guarantee the order across tablets.

For example, let us imagine the following scenario:
* Two rows are being updated concurrently.
* These two rows belong to different tablets.
* The first row `row #1` was updated at time `t1` and the second row `row #2` was updated at time `t2`.

In this case, it is entirely possible for the CDC feature to push the later update corresponding to `row #2` change to Kafka before pushing the update corresponding to `row #1`.

#### At-least once delivery
Updates for rows will be pushed at least once. This can happen in case of tablet leader change where the old leader already pushed changes to Kafka/Elastic Search but the latest pushed op id was not updated in cdc_subscribers table. 

For example, let us imagine a CDC client has received changes for a row at times t1 and t3. It is possible for the client to receive those updates again. 

#### No gaps in change stream
Note that once you have received a change for a row for some timestamp t, you will not receive a previously unseen change for that row at a lower timestamp. Therefore, there is a guarantee at all times that receiving any change implies all older changes have been received for a row.



[![Analytics](https://yugabyte.appspot.com/UA-104956980-4/architecture/design/docdb-change-data-capture.md?pixel&useReferer)](https://github.com/yugabyte/ga-beacon)
