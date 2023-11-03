# Introduction
In databases, change data capture (CDC) is a set of software design patterns used to determine and track the changed data so that action can be taken using the changed data. YugabyteDB CDC captures changes made to data in the database and streams those changes to external processes, applications, or other databases. CDC allows you to track and propagate changes in a YugabyteDB database to downstream consumers based on its Write-Ahead Log (WAL).

# Motivation 
YugabyteDB CDC is built to support the following use cases and motivations. This [GitHub master ticket](https://github.com/yugabyte/yugabyte-db/issues/18724) captures the overall progress of PG compatibility work to support publication, replication slot, and subscriber APIs.

**PostgreSQL compatibility:**
- PostgreSQL has a huge community that needs a PG-compatible API to set up and consume database changes.
- Offer a complete set of PG community connectors for building and managing secure, clean data pipelines, supporting real-time data integrations, and ETL migrations. 
 
**Use cases:**
- Enable microservice-oriented architectures to subscribe to changes:
  - Message buses like Kafka, Google PubSub, AWS Kinesis, etc, are likely for microservices.
  - A search system powered by a service such as Elasticsearch may be used in conjunction with the database that stores the transactions
  - Websocket-based consumption through the HTTP endpoint

- Downstream data warehousing:
  - Write to data warehouses like Snowflake, RedShift, Google BigQuery, etc - for downstream analytics.
  - Generically write to S3 as parquet/JSON/CSV files

# How does the CDC work?

YugabyteDB CDC uses Debezium to capture row-level changes resulting from INSERT, UPDATE, and DELETE operations in the upstream database, and publishes them as events to Kafka using Kafka Connect-compatible connectors.

![What is CDC](/docs/static/images/explore/cdc-overview-what.png)

Debezium is deployed as a set of Kafka Connect-compatible connectors, so you first need to define a YugabyteDB connector configuration and then start the connector by adding it to Kafka Connect.

YugabyteDB automatically splits user tables into multiple shards (also called tablets) using either a hash- or range-based strategy. The primary key for each row in the table uniquely identifies the location of the tablet in the row.

Each tablet has its own WAL file. WAL is NOT in-memory, but it is disk persisted. Each WAL preserves the order in which transactions (or changes) happened. Hybrid TS, Operation ID, and additional metadata about the transaction is also preserved.

![How does CDC work](/docs/static/images/explore/cdc-overview-work2.png)

YugabyteDB normally purges WAL segments after some period of time. This means that the connector does not have the complete history of all changes that have been made to the database. Therefore, when the connector first connects to a particular YugabyteDB database, it starts by performing a consistent snapshot of each of the database schemas.

The Debezium YugabyteDB connector captures row-level changes in the schemas of a YugabyteDB database. The first time it connects to a YugabyteDB cluster, the connector takes a consistent snapshot of all schemas. After that snapshot is complete, the connector continuously captures row-level changes that insert, update, and delete database content, and that were committed to a YugabyteDB database.

![How does CDC work](/docs/static/images/explore/cdc-overview-work.png)

The connector produces a change event for every row-level insert, update, and delete operation that was captured, and sends change event records for each table in a separate Kafka topic. Client applications read the Kafka topics that correspond to the database tables of interest, and can react to every row-level event they receive from those topics. For each table, the default behavior is that the connector streams all generated events to a separate Kafka topic for that table. Applications and services consume data change event records from that topic.

The core primitive of CDC is the _stream_. Streams can be enabled and disabled on databases. Every change to a watched database table is emitted as a record in a configurable format to a configurable sink. Streams scale to any YugabyteDB cluster independent of its size and are designed to impact production traffic as little as possible.

![How does CDC work](/docs/static/images/explore/cdc-overview-work3.png)

# User Journey

This section outlines the various commands and configuration properties set by a user to configure and use CDC streams via the YSQL API. The creation and management of a CDC stream can be done via the YSQL commands.

A CDC stream has two main components:

1. Configuration - Which tables should get streamed. Modeled via **Publication**
2. Checkpoint - Tracks till where the changes have been streamed to the client. Modeled via **Replication Slot**

## Configuration

As mentioned above, the configuration of a stream is modeled via Publication.

### CREATE PUBLICATION

```sql
CREATE PUBLICATION name 
[ FOR ALL TABLES
      | FOR publication_object [, ... ] ]

where publication_object is:
    TABLE table_name 

Parameters:
name: The name of the new publication.
FOR TABLE: Specifies a list of tables to add to the publication. 
FOR ALL TABLES: Marks the publication as one that replicates changes for all tables in the database, including tables created in the future.
```

- CREATE PUBLICATION adds a new publication. The name of the publication should be unique in the database.
- If FOR TABLE or FOR ALL TABLES are not specified, then the publication starts out with an empty set of tables. That is useful if tables are to be added later.
- To create a publication, the invoking user must have the CREATE privilege for the current database. (Of course, superusers bypass this check.)
- To add a table to a publication, the invoking user must have ownership rights on the table. The FOR ALL TABLES clause requires the invoking user to be a superuser.
- Currently to publish a subset of operations (create, update, delete, truncate) via a Publication is not supported.

#### Examples
Create a publication that publishes all changes in two tables:
```sql
CREATE PUBLICATION mypublication FOR TABLE users, departments;
```
Create a publication that publishes all changes in all tables:
```sql
CREATE PUBLICATION alltables FOR ALL TABLES;
```
### ALTER PUBLICATION

```sql
ALTER PUBLICATION name ADD publication_object [, ...]
ALTER PUBLICATION name SET publication_object [, ...]
ALTER PUBLICATION name DROP publication_object [, ...]
ALTER PUBLICATION name OWNER TO { new_owner | CURRENT_ROLE | CURRENT_USER | SESSION_USER }
ALTER PUBLICATION name RENAME TO new_name

where publication_object is one of:
TABLE table_name

Parameters:
name: The name of an existing publication whose definition is to be altered.
table_name: Name of an existing table. 
new_owner: The user name of the new owner of the publication.
new_name: The new name for the publication.
```
- The command ALTER PUBLICATION can change the attributes of a publication.
- The first three variants change which tables are part of the publication. The SET clause will replace the list of tables in the publication with the specified list; the existing tables that were present in the publication will be removed. The ADD and DROP clauses will add and remove one or more tables from the publication. 
- The remaining variants change the owner and the name of the publication.
- You must own the publication to use ALTER PUBLICATION . Adding a table to a publication additionally requires owning that table. To alter the owner, you must also be a direct or indirect member of the new owning role. The new owner must have CREATE privilege on the database. Also, the new owner of a FOR ALL TABLES publication must be a superuser. However, a superuser can change the ownership of a publication regardless of these restrictions.
#### Examples
Add some tables to the publication:
```sql
ALTER PUBLICATION my_publication ADD TABLE users, departments;
```

### DROP PUBLICATION
```sql
DROP PUBLICATION [ IF EXISTS ] name [, ...]

Parameters:
IF EXISTS: Do not throw an error if the publication does not exist. A notice is issued in this case.
name: The name of an existing publication.
```

- DROP PUBLICATION removes an existing publication from the database.
- A publication can only be dropped by its owner or a superuser.
#### Examples
Drop a publication:
```sql
DROP PUBLICATION my_publication;
```

## Checkpointing
### CREATE REPLICATION SLOT
```sql
# Streaming Protocol
CREATE_REPLICATION_SLOT slot_name LOGICAL output_plugin 
[ 
      NOEXPORT_SNAPSHOT | USE_SNAPSHOT 
]
[ WITH RECORD_TYPE record_type]

# Function
pg_create_logical_replication_slot(
  slot_name, 
  output_plugin,
  record_type
)

Parameters:
slot_name: The name of the slot. It must be unique across all databases
output_plugin: The name of the output plugin to be used. The only plugin that will be supported is 'yboutput'.
record_type: This parameter determines the record structure of the data streamed to the client. The valid values are:
  * FULL
  * NOTHING
  * DEFAULT
  * CHANGE_OLD_NEW
  * CHANGE (default)
```

- A Replication Slot can be created either via the streaming protocol command or the standalone function shown above. The name of the replication slot should be unique across all databases.
- The output_plugin parameter should be the name of a valid output plugin. Only yboutput will be supported.
#### Examples
Create a Replication Slot with name test_replication_slot and use the yboutput plugin.
```sql
CREATE_REPLICATION_SLOT test_replication_slot LOGICAL yboutput

pg_create_logical_replication_slot(
  'test_replication_slot', 
  'yboutput'
)
```

Create a Replication Slot with name test_replication_slot and use the yboutput plugin with CHANGE record type.
```sql
CREATE_REPLICATION_SLOT test_replication_slot LOGICAL yboutput WITH RECORD_TYPE FULL

pg_create_logical_replication_slot(
  'test_replication_slot', 
  'yboutput',
  'FULL'
)
```

## DROP REPLICATION SLOT
```sql
# Streaming Protocol
DROP_REPLICATION_SLOT slot_name [WAIT]

# Function
pg_drop_replication_slot(
  slot_name
)

Parameters:
slot_name: The name of the slot to drop
WAIT: This option causes the command to wait if the slot is active until it becomes inactive
 instead of the default behavior of raising an error.
```

- Removes a Replication Slot. A publication can only be dropped by its superuser or a user with replication privileges.
- The WAIT option is treated differently In YugabyteDB. An “active” replication slot means a slot which has been consumed for a certain timeframe.
- We will define this timeframe via a GFlag ysql_replication_slot_activity_threshold with a default of 5 minutes.

#### Examples
Drop an inactive replication slot:
```sql
DROP_REPLICATION_SLOT inactive_replication_slot;
```
Drop a replication slot waiting for it to become inactive:
```sql
DROP_REPLICATION_SLOT active_replication_slot WAIT;
```

## Catalog Objects
### pg_publication
Contains all publication objects contained in the database.

| Column Name | Data Type | Description |
| --- | --- | --- |
| oid | oid | Row identifier |
| pubname | name | Name of the publication |
| pubowner | oid | OID of the owner |
| puballtables | bool | If true, this publication includes all tables in the database including those added in the future. |
| pubinsert | bool | If true, INSERT operations are replicated for tables in the publication. |
| pubupdate | bool | If true, UPDATE operations are replicated for tables in the publication. |
| pubdelete | bool | If true, DELETE operations are replicated for tables in the publication. |
| pubtruncate | bool | If true, TRUNCATE operations are replicated for tables in the publication. |

### pg_publication_rel
Contains mapping between publications and tables. This is a many to many mapping.

| Column Name | Data Type | Description |
| --- | --- | --- |
| oid | oid | Row identifier |
| prpubid | oid | OID of the publication. References pg\_publication.oid |
| prrelid | oid | OID of the relation. References pg\_class.oid |

## Views

### pg_publication_tables
Contains mapping between publications and tables. It is a wrapper over pg\_publication\_rel as it expands the publications defined as FOR ALL TABLES, so for such publications there will be a row for each eligible table.

| Column Name | Data Type | Description |
| --- | --- | --- |
| pubname | name | Name of publication |
| schemaname | name | Name of schema containing table |
| tablename | name | Name of table |

### pg_replication_slots
Provides a listing of all replication slots that currently exist on the database cluster, along with their metadata.

| Column Name | Data Type | Description |
| --- | --- | --- |
| slot_name | name | Name of the replication slot |
| plugin | name | Output plugin name (Always yboutput) |
| slot_type | text | Always logical |
| datoid | oid | The OID of the database this slot is associated with. |
| database | text | The name of the database this slot is associated with. |
| temporary | boolean | True if this is a temporary replication slot. Temporary slots are automatically dropped on error or when the session has finished. |
| active | boolean | True if this slot is currently actively being used. In YugabyteDB, it is difficult to detect activity since the consumption of the replication slot could be happening on another node. So for us, an "active" replication slot means a slot which has been consumed at least once in a certain timeframe. We will define this timeframe via a GFlag ysql_replication_slot_activity_threshold with a default of 5 minutes. |
| active_pid | integer | Not relevant for us. Always 0 The process ID of the session using this slot if the slot is currently actively being used. NULL if inactive. |
| xmin | xid | Irrelevant for us. The oldest transaction that this slot needs the database to retain. VACUUM cannot remove tuples deleted by any later transaction. |
| catalog\_xmin | xid | Irrelevant for us. The oldest transaction affecting the system catalogs that this slot needs the database to retain. VACUUM cannot remove catalog tuples deleted by any later transaction. |
| restart\_lsn | pg\_lsn | Irrelevant for us. The address (LSN) of oldest WAL which still might be required by the consumer of this slot and thus won't be automatically removed during checkpoints. NULL if the LSN of this slot has never been reserved. |
| confirmed\_flush\_lsn | pg\_lsn | Irrelevant for us. The address (LSN) up to which the logical slot's consumer has confirmed receiving data. Data older than this is not available anymore. NULL for physical slots. |
| yb_stream_id | text | UUID of the CDC stream |

## Connector
The YugabyteDB Debezium connector will accept the following parameters:

| Name | Type | Description |
| --- | --- | --- |
| slot.name | String | The name of the replication slot the connector should consume the data from |
| slot.drop.on.stop | Boolean | Whether the replication slot should be dropped once the connector is stopped |
| publication.name | String | (Optional) The name of the Publication describing which tables to consume the changes for |
| publication.autocreate.mode | Boolean | Whether a Publication should be auto created for consumption |

## Overall Journey Example

To consume a snapshot and changes for table t1 and t2, the following steps need to be followed:

1. Create a publication specifying the tables
```sql
CREATE PUBLICATION pub FOR TABLE t1, t2;
```

2. Create a replication slot

```sql
CREATE_REPLICATION_SLOT slot_name LOGICAL yboutput USE_SNAPSHOT
```

3. Start a Debezium connector with the following configuration properties
```sql
slot.name = 'slot_name'
slot.drop.on.stop = 'true'
publication.name = 'pub'
publication.autocreate.mode = 'false'
```
# Future Roadmap
- Logical replication in PostgreSQL uses a publish and subscribe model where one or more subscribers can subscribe to one or more publications. Currently subscribe is Yugabyte’s Debezium connector, but other type of subscribers will also be supported. 

[![Analytics](https://yugabyte.appspot.com/UA-104956980-4/architecture/design/docdb-change-data-capture.md?pixel&useReferer)](https://github.com/yugabyte/ga-beacon)
