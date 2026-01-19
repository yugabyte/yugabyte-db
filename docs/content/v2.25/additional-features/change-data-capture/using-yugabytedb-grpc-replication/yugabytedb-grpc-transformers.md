---
title: YugabyteDB gRPC connector transformers
headerTitle: YugabyteDB gRPC connector transformers
linkTitle: Connector transformers
description: YugabyteDB gRPC connector transformers for Change Data Capture.
menu:
  v2.25:
    parent: debezium-connector-yugabytedb
    identifier: yugabytedb-grpc-connector-transformers
    weight: 50
type: docs
---

The YugabyteDB gRPC Connector comes bundled with Single Message Transformers (SMTs). SMTs are applied to messages as they flow through Kafka Connect so that sinks understand the format in which data is sent. SMTs transform inbound messages after a source connector has produced them, but before they are written to Kafka. SMTs transform outbound messages before they are sent to a sink connector.

The following SMTs are bundled with the connector jar file available on [GitHub releases](https://github.com/yugabyte/debezium-connector-yugabytedb/releases):

* YBExtractNewRecordState
* PGCompatible

To provide examples of output from these transformers, consider a table created using the following statement:

```sql
CREATE TABLE test (id INT PRIMARY KEY, name TEXT, aura INT);
```

The following DML statements are used to demonstrate the payload for each transformer in case of individual replica identities:

```sql
-- statement 1
INSERT INTO test VALUES (1, 'Vaibhav', 9876);

-- statement 2
UPDATE test SET aura = 9999 WHERE id = 1;

-- statement 3
UPDATE test SET name = 'Vaibhav Kushwaha', aura = 10 WHERE id = 1;

-- statement 4
UPDATE test SET aura = NULL WHERE id = 1;

-- statement 5
DELETE FROM test WHERE id = 1;
```

For simplicity, only `before` and `after` fields of the `payload` of the message published by the connector are mentioned in the following example output. Any information pertaining to the record schema, if it is the same as the standard Debezium connector for PostgreSQL, is skipped.

By default, the YugabyteDB CDC service publishes events with a schema that only includes columns that have been modified. The source connector then sends the value as `null` for columns that are missing in the payload. Each column payload includes a `set` field that is used to signal if a column has been set to `null` because it wasn't present in the payload from YugabyteDB.

## YBExtractNewRecordState

**Transformer class:** `io.debezium.connector.yugabytedb.transforms.YBExtractNewRecordState`

The SMT `YBExtractNewRecordState` is used to flatten the records published by the connector and just keep the payload field in a flattened format.

The following examples show what the payload would look like for each [before image mode](../cdc-get-started/#before-image-modes). Note that in this example, as you have set the property `delete.handling.mode` to `none` for the transformer, it will not drop the delete records from the stream. Additionally, this SMT works on the `after` field of the payload and since the `after` field for the DELETE record is `null`, the output after applying this transformer on a DELETE record is also `null`.

### CHANGE

```json{.nocopy}
-- statement 1
{"id":1,"name":"Vaibhav","aura":9876}

-- statement 2
{"id":1,"aura":9999}

-- statement 3
{"id":1,"name":"Vaibhav Kushwaha","aura":10}

-- statement 4
{"id":1,"aura":null}

-- statement 5
null
```

### FULL_ROW_NEW_IMAGE

```json{.nocopy}
-- statement 1
{"id":1,"name":"Vaibhav","aura":9876}

-- statement 2
{"id":1,"name":"Vaibhav","aura":9999}

-- statement 3
{"id":1,"name":"Vaibhav Kushwaha","aura":10}

-- statement 4
{"id":1,"name":"Vaibhav Kushwaha","aura":null}

-- statement 5
null
```

### MODIFIED_COLUMNS_OLD_AND_NEW_IMAGES

```json{.nocopy}
-- statement 1
{"id":1,"name":"Vaibhav","aura":9876}

-- statement 2
{"id":1,"name":null,"aura":9999}

-- statement 3
{"id":1,"name":"Vaibhav Kushwaha","aura":10}

-- statement 4
{"id":1,"name":null,"aura":null}

-- statement 5
null
```

### ALL

```json{.nocopy}
-- statement 1
{"id":1,"name":"Vaibhav","aura":9876}

-- statement 2
{"id":1,"name":"Vaibhav","aura":9999}

-- statement 3
{"id":1,"name":"Vaibhav Kushwaha","aura":10}

-- statement 4
{"id":1,"name":"Vaibhav Kushwaha","aura":null}

-- statement 5
null
```

## PGCompatible

**Transformer class:** `io.debezium.connector.yugabytedb.transforms.PGCompatible`

Some sink connectors may not understand the payload format published by the connector. `PGCompatible` transforms the payload to a format that is compatible with the format of standard change data events. Specifically, it transforms column schema and value to remove the `set` field and collapse the payload such that it only contains the data type schema and value.

`PGCompatible` differs from `YBExtractNewRecordState` by recursively modifying all the fields in a payload.

The following examples show what the payload would look like for each [before image mode](../cdc-get-started/#before-image-modes).

### CHANGE

```json{.nocopy}
-- statement 1
"before":null,"after":{"id":1,"name":"Vaibhav","aura":9876}

-- statement 2
"before":null,"after":{"id":1,"name":null,"aura":9999}

-- statement 3
"before":null,"after":{"id":1,"name":"Vaibhav Kushwaha","aura":10}

-- statement 4
"before":null,"after":{"id":1,"name":null,"aura":null}

-- statement 5
"before":{"id":1,"name":null,"aura":null},"after":null
```

Note that for statements 2 and 4, the columns that were not updated as a part of the UPDATE statement are `null` in the output field.

### FULL_ROW_NEW_IMAGE

```json{.nocopy}
-- statement 1
"before":null,"after":{"id":1,"name":"Vaibhav","aura":9876}

-- statement 2
"before":null,"after":{"id":1,"name":"Vaibhav","aura":9999}

-- statement 3
"before":null,"after":{"id":1,"name":"Vaibhav Kushwaha","aura":10}

-- statement 4
"before":null,"after":{"id":1,"name":"Vaibhav Kushwaha","aura":null}

-- statement 5
"before":{"id":1,"name":"Vaibhav Kushwaha","aura":null},"after":null
```

### MODIFIED_COLUMNS_OLD_AND_NEW_IMAGES

```json{.nocopy}
-- statement 1
"before":null,"after":{"id":1,"name":"Vaibhav","aura":9876}

-- statement 2
"before":{"id":1,"name":null,"aura":9876},"after":{"id":1,"name":null,"aura":9999}

-- statement 3
"before":{"id":1,"name":"Vaibhav","aura":9999},"after":{"id":1,"name":"Vaibhav Kushwaha","aura":10}

-- statement 4
"before":{"id":1,"name":null,"aura":10},"after":{"id":1,"name":null,"aura":null}

-- statement 5
"before":{"id":1,"name":null,"aura":null},"after":null
```

### ALL

```json{.nocopy}
-- statement 1
"before":null,"after":{"id":1,"name":"Vaibhav","aura":9876}

-- statement 2
"before":{"id":1,"name":"Vaibhav","aura":9876},"after":{"id":1,"name":"Vaibhav","aura":9999}

-- statement 3
"before":{"id":1,"name":"Vaibhav","aura":9999},"after":{"id":1,"name":"Vaibhav Kushwaha","aura":10}

-- statement 4
"before":{"id":1,"name":"Vaibhav Kushwaha","aura":10},"after":{"id":1,"name":"Vaibhav Kushwaha","aura":null}

-- statement 5
"before":{"id":1,"name":"Vaibhav Kushwaha","aura":null},"after":null
```
