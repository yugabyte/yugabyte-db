---
title: YugabyteDB connector properties
headerTitle: YugabyteDB connector properties
linkTitle: Connector properties
description: YugabyteDB connector properties for Change Data Capture in YugabyteDB.
menu:
  v2.25:
    parent: yugabytedb-connector
    identifier: yugabytedb-connector-properties
    weight: 70
type: docs
---

The connector has many configuration properties that you can use to achieve the right connector behavior for your application. Many properties have default values.

## Required configuration properties

The following configuration properties are _required_ unless a default value is available.

##### name

Unique name for the connector. Attempting to register again with the same name will fail. This property is required by all Kafka Connect connectors.

No default.

##### connector.class

The name of the Java class for the connector. Always use a value of `io.debezium.connector.postgresql.YugabyteDBConnector` for the YugabyteDB connector.

No default.

##### tasks.max

The maximum number of tasks that should be created for this connector. The YugabyteDB connector always uses a single task and therefore does not use this value, so the default is always acceptable.

Default: 1

##### plugin.name

The name of the YugabyteDB [logical decoding plugin](../key-concepts/#output-plugin) installed on the YugabyteDB server.

Supported values are `yboutput`, and `pgoutput`.

Default: `decoderbufs`

##### slot.name

The name of the YugabyteDB logical decoding slot that was created for streaming changes from a particular plugin for a particular database/schema. The server uses this slot to stream events to the Debezium connector that you are configuring.

Slot names can contain lower-case letters, numbers, and the underscore character.

Default: `debezium`

##### slot.drop.on.stop

Whether or not to delete the logical replication slot when the connector stops in a graceful, expected way. The default behavior is that the replication slot remains configured for the connector when the connector stops. When the connector restarts, having the same replication slot enables the connector to start processing where it left off.

Set to true in only testing or development environments. Dropping the slot allows the database to discard WAL segments. When the connector restarts it performs a new snapshot or it can continue from a persistent offset in the Kafka Connect offsets topic.

Default: false

##### publication.name

The name of the YugabyteDB publication created for streaming changes when using `pgoutput`.

This publication is created at start-up if it does not already exist and it includes all tables. Debezium then applies its own include/exclude list filtering, if configured, to limit the publication to change events for the specific tables of interest. The connector user must have superuser permissions to create this publication, so it is usually preferable to create the publication before starting the connector for the first time.

If the publication already exists, either for all tables or configured with a subset of tables, Debezium uses the publication as it is defined.

Default: `dbz_publication`

##### database.hostname

IP address or hostname of the YugabyteDB database server. This needs to be in the format `IP1:PORT1,IP2:PORT2,IP3:PORT3`

No default.

##### database.port

Integer port number of the YugabyteDB database server.

Default: 5433

##### database.user

Name of the YugabyteDB database user for connecting to the YugabyteDB database server.

No default.

##### database.password

Password to use when connecting to the YugabyteDB database server.

No default.

##### database.dbname

The name of the YugabyteDB database from which to stream the changes.

No default.

##### topic.prefix

Topic prefix that provides a namespace for the particular YugabyteDB database server or cluster in which Debezium is capturing changes. The prefix should be unique across all other connectors, as it is used as a topic name prefix for all Kafka topics that receive records from this connector. Only alphanumeric characters, hyphens, dots, and underscores must be used in the database server logical name.

{{< warning title="Warning" >}} Do not change the value of this property. If you change the name value, after a restart, instead of continuing to emit events to the original topics, the connector emits subsequent events to topics whose names are based on the new value. {{< /warning >}}

No default.

##### schema.include.list

An optional, comma-separated list of regular expressions that match names of schemas for which you **want** to capture changes. Any schema name not included in `schema.include.list` is excluded from having its changes captured. By default, all non-system schemas have their changes captured.

To match the name of a schema, Debezium applies the regular expression that you specify as an _anchored_ regular expression. That is, the specified expression is matched against the entire identifier for the schema; it does not match substrings that might be present in a schema name.

If you include this property in the configuration, do not also set the `schema.exclude.list` property.

No default.

##### schema.exclude.list

An optional, comma-separated list of regular expressions that match names of schemas for which you **do not** want to capture changes. Any schema whose name is not included in `schema.exclude.list` has its changes captured, with the exception of system schemas.

To match the name of a schema, Debezium applies the regular expression that you specify as an _anchored_ regular expression. That is, the specified expression is matched against the entire identifier for the schema; it does not match substrings that might be present in a schema name.

If you include this property in the configuration, do not set the `schema.include.list` property.

No default.

##### table.include.list

An optional, comma-separated list of regular expressions that match fully-qualified table identifiers for tables whose changes you want to capture. When this property is set, the connector captures changes only from the specified tables. Each identifier is of the form `schemaName.tableName`. By default, the connector captures changes in every non-system table in each schema whose changes are being captured.

To match the name of a table, Debezium applies the regular expression that you specify as an anchored regular expression. That is, the specified expression is matched against the entire identifier for the table; it does not match substrings that might be present in a table name.

If you include this property in the configuration, do not also set the `table.exclude.list` property.

No default.

##### table.exclude.list

An optional, comma-separated list of regular expressions that match fully-qualified table identifiers for tables whose changes you do not want to capture. Each identifier is of the form `schemaName.tableName`. When this property is set, the connector captures changes from every table that you do not specify.

To match the name of a table, Debezium applies the regular expression that you specify as an _anchored_ regular expression. That is, the specified expression is matched against the entire identifier for the table; it does not match substrings that might be present in a table name.

If you include this property in the configuration, do not set the `table.include.list` property.

No default.

##### column.include.list

An optional, comma-separated list of regular expressions that match the fully-qualified names of columns that should be included in change event record values. Fully-qualified names for columns are of the form `schemaName.tableName.columnName`.

To match the name of a column, Debezium applies the regular expression that you specify as an _anchored_ regular expression. That is, the expression is used to match the entire name string of the column; it does not match substrings that might be present in a column name.

If you include this property in the configuration, do not also set the `column.exclude.list` property.

No default.

##### column.exclude.list

An optional, comma-separated list of regular expressions that match the fully-qualified names of columns that should be excluded from change event record values. Fully-qualified names for columns are of the form `schemaName.tableName.columnName`.

To match the name of a column, Debezium applies the regular expression that you specify as an _anchored_ regular expression. That is, the expression is used to match the entire name string of the column; it does not match substrings that might be present in a column name.

If you include this property in the configuration, do not set the `column.include.list` property.

No default

##### skip.messages.without.change

Specifies whether to skip publishing messages when there is no change in included columns. This would essentially filter messages if there is no change in columns included as per `column.include.list` or `column.exclude.list` properties.

Note: Only works when REPLICA IDENTITY of the table is set to FULL.

Default: false

##### time.precision.mode

Time, date, and timestamps can be represented with different kinds of precision:

* `adaptive` captures the time and timestamp values exactly as in the database using either millisecond, microsecond, or nanosecond precision values based on the database column's type.
* `adaptive_time_microseconds` captures the date, datetime and timestamp values exactly as in the database using either millisecond, microsecond, or nanosecond precision values based on the database column's type. An exception is `TIME` type fields, which are always captured as microseconds.
* `connect` always represents time and timestamp values by using Kafka Connect built-in representations for `Time`, `Date`, and `Timestamp`, which use millisecond precision regardless of the database columns' precision.

For more information, see [Temporal types](#temporal-types).

Default: adaptive

##### decimal.handling.mode

Specifies how the connector should handle values for `DECIMAL` and `NUMERIC` columns:

* `double` represents values by using double values, which might result in a loss of precision but which is easier to use.
* `string` encodes values as formatted strings, which are easy to consume but semantic information about the real type is lost.

For more information, see [Decimal types](#decimal-types).

Default: precise

##### interval.handling.mode

Specifies how the connector should handle values for interval columns:

* `numeric` represents intervals using approximate number of microseconds.
* `string` represents intervals exactly by using the string pattern representation `P<years>Y<months>M<days>DT<hours>H<minutes>M<seconds>S`. For example: `P1Y2M3DT4H5M6.78S`.

For more information, see [Basic types](#basic-types).

Default: numeric

##### database.sslmode

Whether to use an encrypted connection to the YugabyteDB server. Options include:

* `disable` uses an unencrypted connection.
* `allow` attempts to use an unencrypted connection first and, failing that, a secure (encrypted) connection.
* `prefer` attempts to use a secure (encrypted) connection first and, failing that, an unencrypted connection.
* `require` uses a secure (encrypted) connection, and fails if one cannot be established.
* `verify-ca` behaves like require but also verifies the server TLS certificate against the configured Certificate Authority (CA) certificates, or fails if no valid matching CA certificates are found.
* `verify-full` behaves like verify-ca but also verifies that the server certificate matches the host to which the connector is trying to connect.

For more information, see the [PostgreSQL documentation](https://www.postgresql.org/docs/15/static/libpq-connect.html).

Default: prefer

##### database.sslcert

The path to the file that contains the SSL certificate for the client. For more information, see the [PostgreSQL documentation](https://www.postgresql.org/docs/15/static/libpq-connect.html).

No default.

##### database.sslkey

The path to the file that contains the SSL private key of the client. For more information, see the [PostgreSQL documentation](https://www.postgresql.org/docs/15/static/libpq-connect.html).

No default.

##### database.sslpassword

The password to access the client private key from the file specified by `database.sslkey`. For more information, see the [PostgreSQL documentation](https://www.postgresql.org/docs/15/static/libpq-connect.html).

No default.

##### database.sslrootcert

The path to the file that contains the root certificate(s) against which the server is validated. For more information, see the [PostgreSQL documentation](https://www.postgresql.org/docs/15/static/libpq-connect.html).

No default.

##### database.tcpKeepAlive

Enable TCP keep-alive probe to verify that the database connection is still alive. For more information, see the [PostgreSQL documentation](https://www.postgresql.org/docs/15/static/libpq-connect.html).

Default: true

##### tombstones.on.delete

Controls whether a delete event is followed by a tombstone event.

* `true` - a delete operation is represented by a delete event and a subsequent tombstone event.

* `false` - only a delete event is emitted.

After a source record is deleted, emitting a tombstone event (the default behavior) allows Kafka to completely delete all events that pertain to the key of the deleted row in case log compaction is enabled for the topic.

Default: true

##### column.truncate.to.length.chars

An optional, comma-separated list of regular expressions that match the fully-qualified names of character-based columns. Set this property if you want to truncate the data in a set of columns when it exceeds the number of characters specified by the length in the property name. Set `length` to a positive integer value, for example, `column.truncate.to.20.chars`.

The fully-qualified name of a column observes the following format: `<schemaName>.<tableName>.<columnName>`. To match the name of a column, Debezium applies the regular expression that you specify as an anchored regular expression. That is, the specified expression is matched against the entire name string of the column; the expression does not match substrings that might be present in a column name.

You can specify multiple properties with different lengths in a single configuration.

Default: n/a

##### column.mask.with.length.chars

An optional, comma-separated list of regular expressions that match the fully-qualified names of character-based columns. Set this property if you want the connector to mask the values for a set of columns, for example, if they contain sensitive data. Set `length` to a positive integer to replace data in the specified columns with the number of asterisk (`*`) characters specified by the length in the property name. Set length to `0` (zero) to replace data in the specified columns with an empty string.

The fully-qualified name of a column observes the following format: schemaName.tableName.columnName. To match the name of a column, Debezium applies the regular expression that you specify as an anchored regular expression. That is, the specified expression is matched against the entire name string of the column; the expression does not match substrings that might be present in a column name.

You can specify multiple properties with different lengths in a single configuration.

Default: n/a

##### column.mask.hash.hashAlgorithm.with.salt._salt_;<br/>column.mask.hash.v2.hashAlgorithm.with.salt._salt_

An optional, comma-separated list of regular expressions that match the fully-qualified names of character-based columns. Fully-qualified names for columns are of the form `<schemaName>.<tableName>.<columnName>`.

To match the name of a column Debezium applies the regular expression that you specify as an anchored regular expression. That is, the specified expression is matched against the entire name string of the column; the expression does not match substrings that might be present in a column name. In the resulting change event record, the values for the specified columns are replaced with pseudonyms.

A pseudonym consists of the hashed value that results from applying the specified hashAlgorithm and salt. Based on the hash function that is used, referential integrity is maintained, while column values are replaced with pseudonyms. Supported hash functions are described in the [MessageDigest](https://docs.oracle.com/javase/7/docs/technotes/guides/security/StandardNames.html#MessageDigest) section of the Java Cryptography Architecture Standard Algorithm Name Documentation.

In the following example, `CzQMA0cB5K` is a randomly selected salt:

```column.mask.hash.SHA-256.with.salt.CzQMA0cB5K = inventory.orders.customerName, inventory.shipment.customerName```

If necessary, the pseudonym is automatically shortened to the length of the column. The connector configuration can include multiple properties that specify different hash algorithms and salts.

Depending on the `hashAlgorithm` used, the salt selected, and the actual data set, the resulting data set might not be completely masked.

Hashing strategy version 2 should be used to ensure fidelity if the value is being hashed in different places or systems.

Default: n/a

##### column.propagate.source.type

An optional, comma-separated list of regular expressions that match the fully-qualified names of columns for which you want the connector to emit extra parameters that represent column metadata. When this property is set, the connector adds the following fields to the schema of event records:

* __debezium.source.column.type
* __debezium.source.column.length
* __debezium.source.column.scale

These parameters propagate a column's original type name and length (for variable-width types), respectively.

Enabling the connector to emit this extra data can assist in properly sizing specific numeric or character-based columns in sink databases.

The fully-qualified name of a column observes one of the following formats: `databaseName.tableName.columnName`, or `databaseName.schemaName.tableName.columnName`.

To match the name of a column, Debezium applies the regular expression that you specify as an _anchored_ regular expression. That is, the specified expression is matched against the entire name string of the column; the expression does not match substrings that might be present in a column name.

Default: n/a

##### datatype.propagate.source.type

An optional, comma-separated list of regular expressions that specify the fully-qualified names of data types that are defined for columns in a database. When this property is set, for columns with matching data types, the connector emits event records that include the following extra fields in their schema:

* __debezium.source.column.type
* __debezium.source.column.length
* __debezium.source.column.scale

These parameters propagate a column's original type name and length (for variable-width types), respectively.

Enabling the connector to emit this extra data can assist in properly sizing specific numeric or character-based columns in sink databases.

The fully-qualified name of a column observes one of the following formats: `databaseName.tableName.typeName`, or `databaseName.schemaName.tableName.typeName`.

To match the name of a data type, Debezium applies the regular expression that you specify as an _anchored_ regular expression. That is, the specified expression is matched against the entire name string of the data type; the expression does not match substrings that might be present in a type name.

For the list of YugabyteDB-specific data type names, see [Data type mappings](#data-type-mappings).

Default: n/a

##### message.key.columns

A list of expressions that specify the columns that the connector uses to form custom message keys for change event records that it publishes to the Kafka topics for specified tables.

By default, Debezium uses the primary key column of a table as the message key for records that it emits. In place of the default, or to specify a key for tables that lack a primary key, you can configure custom message keys based on one or more columns.

To establish a custom message key for a table, list the table, followed by the columns to use as the message key. Each list entry takes the following format:

`<fully-qualified_tableName>:<keyColumn>,<keyColumn>`

To base a table key on multiple column names, insert commas between the column names.

Each fully-qualified table name is a regular expression in the format `<schemaName>.<tableName>`.

The property can include entries for multiple tables. Use a semicolon to separate table entries in the list.

The following example sets the message key for the tables `inventory.customers` and `purchase.orders`:

`inventory.customers:pk1,pk2;(.*).purchaseorders:pk3,pk4`

For the table `inventory.customer`, the columns `pk1` and `pk2` are specified as the message key. For the `purchaseorders` tables in any schema, the columns `pk3` and `pk4` server as the message key.

There is no limit to the number of columns that you use to create custom message keys. However, it's best to use the minimum number that are required to specify a unique key.

Note that having this property set and `REPLICA IDENTITY` set to `DEFAULT` on the tables, will cause the tombstone events to not be created properly if the key columns are not part of the primary key of the table. Setting `REPLICA IDENTITY` to `FULL` is the only solution.

Default: _empty string_

##### publication.autocreate.mode

Applies only when streaming changes by using the [pgoutput plugin](https://www.postgresql.org/docs/15/sql-createpublication.html). The setting determines how creation of a [publication](https://www.postgresql.org/docs/15/logical-replication-publication.html) should work. Specify one of the following values:

* `all_tables` - If a publication exists, the connector uses it. If a publication does not exist, the connector creates a publication for all tables in the database for which the connector is capturing changes. For the connector to create a publication it must access the database through a database user account that has permission to create publications and perform replications. You grant the required permission by using the following SQL command `CREATE PUBLICATION <publication_name> FOR ALL TABLES;`.
* `disabled` - The connector does not attempt to create a publication. A database administrator or the user configured to perform replications must have created the publication before running the connector. If the connector cannot find the publication, the connector throws an exception and stops.
* `filtered` - If a publication exists, the connector uses it. If no publication exists, the connector creates a new publication for tables that match the current filter configuration as specified by the `schema.include.list`, `schema.exclude.list`, and `table.include.list`, and `table.exclude.list` connector configuration properties.

    For example: `CREATE PUBLICATION <publication_name> FOR TABLE <tbl1, tbl2, tbl3>`. If the publication exists, the connector updates the publication for tables that match the current filter configuration. For example: `ALTER PUBLICATION <publication_name> SET TABLE <tbl1, tbl2, tbl3>`.

Default: `all_tables`

##### replica.identity.autoset.values

The setting determines the value for [replica identity](#replica-identity) at table level.

This option will overwrite the existing value in database. A comma-separated list of regular expressions that match fully-qualified tables and replica identity value to be used in the table.

Each expression must match the pattern `<fully-qualified table name>:<replica identity>`, where the table name could be defined as (`SCHEMA_NAME.TABLE_NAME`), and the replica identity values are:

* `DEFAULT` - Records the old values of the columns of the primary key, if any. This is the default for non-system tables.
* `FULL` - Records the old values of all columns in the row
* `NOTHING` - Records no information about the old row. This is the default for system tables.

    For example:

    ```json
    schema1.*:FULL,schema2.table2:NOTHING,schema2.table3:DEFAULT
    ```

{{< warning title="Warning" >}} Tables in YugabyteDB will always have the replica identity present at the time of replication slot creation, it cannot be altered at runtime. If it needs to be altered, it will only be reflected on a new slot created after altering the replica identity. {{< /warning >}}

Default: _empty string_

##### binary.handling.mode

Specifies how binary (`bytea`) columns should be represented in change events:

* `bytes` represents binary data as byte array.
* `base64` represents binary data as base64-encoded strings.
`base64-url-safe` represents binary data as base64-url-safe-encoded strings.
* `hex` represents binary data as hex-encoded (base16) strings.

Default: bytes

##### schema.name.adjustment.mode

Specifies how schema names should be adjusted for compatibility with the message converter used by the connector. Possible settings:

* `none` does not apply any adjustment.
* `avro` replaces the characters that cannot be used in the Avro type name with underscore.
* `avro_unicode` replaces the underscore or characters that cannot be used in the Avro type name with corresponding unicode like _uxxxx. Note: `_` is an escape sequence like backslash in Java.

No default.

##### field.name.adjustment.mode

Specifies how field names should be adjusted for compatibility with the message converter used by the connector. Possible settings:

* `none` does not apply any adjustment.
* `avro` replaces the characters that cannot be used in the Avro type name with underscore.
* `avro_unicode` replaces the underscore or characters that cannot be used in the Avro type name with corresponding unicode like _uxxxx. Note: `_` is an escape sequence like backslash in Java.

For more information, see [Avro naming](https://debezium.io/documentation/reference/2.5/configuration/avro.html#avro-naming).

No default.

##### money.fraction.digits

Specifies how many decimal digits should be used when converting PostgreSQL `money` type to `java.math.BigDecimal`, which represents the values in change events. Applicable only when `decimal.handling.mode` is set to `precise`.

Default: 2

## Advanced configuration properties

The following advanced configuration properties have defaults that work in most situations and therefore rarely need to be specified in the connector configuration.

##### converters

Enumerates a comma-separated list of the symbolic names of the custom converter instances that the connector can use. For example, `isbn`.

You must set the converters property to enable the connector to use a custom converter.

For each converter that you configure for a connector, you must also add a `.type` property, which specifies the fully-qualified name of the class that implements the converter interface. The `.type` property uses the following format:

```properties
<converterSymbolicName>.type
```

For example:

```properties
isbn.type: io.debezium.test.IsbnConverter
```

If you want to further control the behavior of a configured converter, you can add one or more configuration parameters to pass values to the converter. To associate any additional configuration parameter with a converter, prefix the parameter names with the symbolic name of the converter. For example,

```properties
isbn.schema.name: io.debezium.YugabyteDB.type.Isbn
```

No default.

##### snapshot.mode

Specifies the criteria for performing a snapshot when the connector starts:

* `initial` - The connector performs a snapshot only when no offsets have been recorded for the logical server name.
* `never` - The connector never performs snapshots. When a connector is configured this way, its behavior when it starts is as follows. If there is a previously stored Log Sequence Number ([LSN](../key-concepts/#lsn-type)) in the Kafka offsets topic, the connector continues streaming changes from that position. If no LSN has been stored, the connector starts streaming changes from the point in time when the YugabyteDB logical replication slot was created on the server. The never snapshot mode is useful only when you know all data of interest is still reflected in the WAL.
* `initial_only` - The connector performs an initial snapshot and then stops, without processing any subsequent changes.

Default: `initial`

##### snapshot.include.collection.list

An optional, comma-separated list of regular expressions that match the fully-qualified names (`<schemaName>.<tableName>`) of the tables to include in a snapshot. The specified items must be named in the connector's `table.include.list` property. This property takes effect only if the connector's `snapshot.mode` property is set to a value other than `never`.

This property does not affect the behavior of incremental snapshots.

To match the name of a table, Debezium applies the regular expression that you specify as an _anchored_ regular expression. That is, the specified expression is matched against the entire name string of the table; it does not match substrings that might be present in a table name.

Default: All tables included in `table.include.list`

##### snapshot.select.statement.overrides

Specifies the table rows to include in a snapshot. Use the property if you want a snapshot to include only a subset of the rows in a table. This property affects snapshots only. It does not apply to events that the connector reads from the log.

The property contains a comma-separated list of fully-qualified table names in the form `<schemaName>.<tableName>`. For example:

```properties
"snapshot.select.statement.overrides": "inventory.products,customers.orders"
```

For each table in the list, add a further configuration property that specifies the `SELECT` statement for the connector to run on the table when it takes a snapshot. The specified `SELECT` statement determines the subset of table rows to include in the snapshot. Use the following format to specify the name of this `SELECT` statement property:

```properties
snapshot.select.statement.overrides.<schemaName>.<tableName>
```

For example:

```properties
snapshot.select.statement.overrides.customers.orders
```

For example, from a `customers.orders` table that includes the soft-delete column `delete_flag`, add the following properties if you want a snapshot to include only those records that are not soft-deleted:

```properties
"snapshot.select.statement.overrides": "customer.orders",
"snapshot.select.statement.overrides.customer.orders": "SELECT * FROM [customers].[orders] WHERE delete_flag = 0 ORDER BY id DESC"
```

In the resulting snapshot, the connector includes only the records for which `delete_flag = 0`.

No default.

##### event.processing.failure.handling.mode

Specifies how the connector should react to exceptions during processing of events:

* `fail` propagates the exception, indicates the offset of the problematic event, and causes the connector to stop.
* `warn` logs the offset of the problematic event, skips that event, and continues processing.
* `skip` skips the problematic event and continues processing.

Default: fail

##### max.batch.size

Positive integer value that specifies the maximum size of each batch of events that the connector processes.

Default: 2048

##### max.queue.size

Positive integer value that specifies the maximum number of records that the blocking queue can hold. When Debezium reads events streamed from the database, it places the events in the blocking queue before it writes them to Kafka. The blocking queue can provide backpressure for reading change events from the database in cases where the connector ingests messages faster than it can write them to Kafka, or when Kafka becomes unavailable. Events that are held in the queue are disregarded when the connector periodically records offsets. Always set the value of `max.queue.size` to be larger than the value of `max.batch.size`.

Default: 8192

##### max.queue.size.in.bytes

A long integer value that specifies the maximum volume of the blocking queue in bytes. By default, volume limits are not specified for the blocking queue. To specify the number of bytes that the queue can consume, set this property to a positive long value.

If `max.queue.size` is also set, writing to the queue is blocked when the size of the queue reaches the limit specified by either property. For example, if you set `max.queue.size=1000`, and `max.queue.size.in.bytes=5000`, writing to the queue is blocked after the queue contains 1000 records, or after the volume of the records in the queue reaches 5000 bytes.

Default: 0

##### poll.interval.ms

Positive integer value that specifies the number of milliseconds the connector should wait for new change events to appear before it starts processing a batch of events. Defaults to 500 milliseconds.

Default: 500

##### include.unknown.datatypes

Specifies connector behavior when the connector encounters a field whose data type is unknown. The default behavior is that the connector omits the field from the change event and logs a warning.

Set this property to `true` if you want the change event to contain an opaque binary representation of the field. This lets consumers decode the field. You can control the exact representation by setting the [binary handling mode](#connector-properties) property.

{{< note title="Note" >}} Consumers risk backward compatibility issues when `include.unknown.datatypes` is set to `true`. Not only may the database-specific binary representation change between releases, but if the data type is eventually supported by Debezium, the data type will be sent downstream in a logical type, which would require adjustments by consumers. In general, when encountering unsupported data types, create a feature request so that support can be added. {{< /note >}}

Default: false

##### database.initial.statements

A semicolon separated list of SQL statements that the connector executes when it establishes a JDBC connection to the database. To use a semicolon as a character and not as a delimiter, specify two consecutive semicolons, `;;`.

The connector may establish JDBC connections at its own discretion. Consequently, this property is useful for configuring session parameters only, and not for executing DML statements.

The connector does not execute these statements when it creates a connection for reading the transaction log.

No default

##### status.update.interval.ms

Frequency for sending replication connection status updates to the server, given in milliseconds. The property also controls how frequently the database status is checked to detect a dead connection in case the database was shut down.

Default: 10000

##### schema.refresh.mode

Specify the conditions that trigger a refresh of the in-memory schema for a table.

`columns_diff` is the safest mode. It ensures that the in-memory schema stays in sync with the database table's schema at all times.

`columns_diff_exclude_unchanged_toast` instructs the connector to refresh the in-memory schema cache if there is a discrepancy with the schema derived from the incoming message, unless unchanged TOASTable data fully accounts for the discrepancy.

This setting can significantly improve connector performance if there are frequently-updated tables that have TOASTed data that are rarely part of updates. However, it is possible for the in-memory schema to become outdated if TOASTable columns are dropped from the table.

Default: `columns_diff`

##### snapshot.delay.ms

An interval in milliseconds that the connector should wait before performing a snapshot when the connector starts. If you are starting multiple connectors in a cluster, this property is useful for avoiding snapshot interruptions, which might cause re-balancing of connectors.

No default

##### snapshot.fetch.size

During a snapshot, the connector reads table content in batches of rows. This property specifies the maximum number of rows in a batch.

Default: 10240

##### slot.stream.params

Semicolon separated list of parameters to pass to the configured logical decoding plugin.

No default

##### slot.max.retries

If connecting to a replication slot fails, this is the maximum number of consecutive attempts to connect.

Default: 6

##### slot.retry.delay.ms

The number of milliseconds to wait between retry attempts when the connector fails to connect to a replication slot.

Default: 10000 (10 seconds)

##### unavailable.value.placeholder

Specifies the constant that the connector provides to indicate that the original value is a toasted value that is not provided by the database. If the setting of `unavailable.value.placeholder` starts with the `hex:` prefix it is expected that the rest of the string represents hexadecimally encoded octets.

Default: `__debezium_unavailable_value`

##### provide.transaction.metadata

Determines whether the connector generates events with transaction boundaries and enriches change event envelopes with transaction metadata. Specify true if you want the connector to do this. For more information, see [Transaction metadata](#transaction-metadata).

Default: false

##### flush.lsn.source

Determines whether the connector should commit the LSN of the processed records in the source YugabyteDB database so that the WAL logs can be deleted. Specify `false` if you don't want the connector to do this. Please note that if set to `false` LSN will not be acknowledged by Debezium and as a result WAL logs will not be cleared which might result in disk space issues. User is expected to handle the acknowledgement of LSN outside Debezium.

Default: true

##### retriable.restart.connector.wait.ms

The number of milliseconds to wait before restarting a connector after a retriable error occurs.

Default: 30000 (30 seconds)

##### skipped.operations

A comma-separated list of operation types that will be skipped during streaming. The operations include: `c` for inserts/create, `u` for updates, `d` for deletes, `t` for truncates, and `none` to not skip any operations. By default, truncate operations are skipped.

Default: t

##### xmin.fetch.interval.ms

How often, in milliseconds, the XMIN will be read from the replication slot. The XMIN value provides the lower bounds of where a new replication slot could start from. The default value of `0` disables tracking XMIN tracking.

Default: 0

##### topic.naming.strategy

The name of the TopicNamingStrategy class that should be used to determine the topic name for data change, schema change, transaction, heartbeat event etc., defaults to `SchemaTopicNamingStrategy`.

Default: `io.debezium.schema.SchemaTopicNamingStrategy`

##### topic.delimiter

Specify the delimiter for topic name.

Default: `.`

##### topic.cache.size

The size used for holding the topic names in bounded concurrent hash map. This cache will help to determine the topic name corresponding to a given data collection.

Default: 10000

##### topic.heartbeat.prefix

Controls the name of the topic to which the connector sends heartbeat messages. The topic name has this pattern:

`<topic.heartbeat.prefix>.<topic.prefix>`

For example, if the topic prefix is `fulfillment`, the default topic name is `__debezium-heartbeat.fulfillment`.

Default: `__debezium-heartbeat`

##### topic.transaction

Controls the name of the topic to which the connector sends transaction metadata messages. The topic name has this pattern:

`<topic.prefix>.<topic.transaction>`

For example, if the `topic.prefix` is `fulfillment`, the default topic name is `fulfillment.transaction`.

Default: transaction

##### snapshot.max.threads

Specifies the number of threads that the connector uses when performing an initial snapshot. To enable parallel initial snapshots, set the property to a value greater than 1. In a parallel initial snapshot, the connector processes multiple tables concurrently. This feature is incubating.

Default: 1

##### custom.metric.tags

The custom metric tags will accept key-value pairs to customize the MBean object name which should be appended the end of regular name, each key would represent a tag for the MBean object name, and the corresponding value would be the value of that tag the key is. For example: `k1=v1,k2=v2`.

No default

##### errors.max.retries

The maximum number of retries on retriable errors (for example, connection errors) before failing (-1 = no limit, 0 = disabled, > 0 = num of retries).

Default: 60

##### slot.lsn.type

The type of LSN to use for the specified replication slot:

* SEQUENCE - A monotonic increasing number that determines the record in global order in the context of a slot.
* HYBRID_TIME - A hybrid time value that can be used to compare transactions across slots.

##### streaming.mode

Specifies whether the connector should stream changes using a single slot or multiple slots in parallel.

* `default` uses a single task to stream all changes.
* `parallel` uses multi task mode and streams changes using the number of specified replication slots.

{{< note title="Important" >}}

When deploying the connector using `parallel` streaming mode, you need to ensure that the `table.include.list` only contains one table for which the streaming is supposed to happen in parallel.

{{< /note >}}

{{< note title="Usage with snapshot" >}}

If `snapshot.mode` is set to `initial` or `initial_only`, you need to ensure that the configuration also contains a valid value for the configuration property `primary.key.hash.columns`.

{{< /note >}}

##### slot.names

A list of slot names, provided as comma-separated values, to be used by each task when using `streaming.mode=parallel`. This property applies only when `streaming.mode` is set to `parallel`; otherwise it has no effect.

No default.

##### publication.names

A list of publication names, provided as comma-separated values, to be used by each task when using `streaming.mode=parallel`. This property applies only when `streaming.mode` is set to `parallel`; otherwise it has no effect.

No default.

##### slot.ranges

A range of slots to be used by each task when using `streaming.mode=parallel`, provided as tablet hash code ranges separated by semi colons. This property applies only when `streaming.mode` is set to `parallel`; otherwise it has no effect.

No default.

For example, suppose you have a table with 3 tablets where the tablets have hash ranges of `[0,21845)`, `[21845,43690)`, and `[43690,65536)`. The value for this configuration would be `slot.ranges=0,21845;21845,43690;43690,65536`.

##### primary.key.hash.columns

The columns of the table which constitute the hash part of the primary key. This property is only valid when `streaming.mode` is set to `parallel`.

## Pass-through configuration properties

The connector also supports pass-through configuration properties that are used when creating the Kafka producer and consumer.

Be sure to consult the [Kafka documentation](https://kafka.apache.org/documentation.html) for all of the configuration properties for Kafka producers and consumers. The YugabyteDB connector does use the [new consumer configuration properties](https://kafka.apache.org/documentation.html#consumerconfigs).
