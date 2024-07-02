---
title: ybm CLI region resource
headerTitle: ybm region
linkTitle: region
description: YugabyteDB Aeon CLI reference Region resource.
headcontent: Query regions and instance types
menu:
  preview_yugabyte-cloud:
    identifier: managed-cli-region
    parent: managed-cli-reference
    weight: 20
type: docs
---

Use the `region` resource to query [cloud provider region](../../../../cloud-basics/create-clusters-overview/#cloud-provider-regions) information.

## Syntax

```text
Usage: ybm region [command] [flags]
```

## Examples

List AWS regions:

```sh
ybm region list \
  --cloud-provider AWS
```

List AWS instance types in us-west-2:

```sh
ybm region instance list \
  --cloud-provider AWS \
  --region us-west-2
```

## Commands

### list

List the regions available for the specified cloud provider.

| Flag | Description |
| :--- | :--- |
| --cloud-provider | Required. Cloud provider. `AWS`, `AZURE`, or `GCP`. |

### instance list

List the available instance types for the specified cloud provider and region.

| Flag | Description |
| :--- | :--- |
| --cloud-provider | Required. Cloud provider. `AWS`, `AZURE`, or `GCP`. |
| --region | Required. The region from which to fetch instance types. |
| --tier | Type of cluster. `Sandbox` or `Dedicated` (default). |
| --show-disabled | Whether to show disabled instance types. `true` (default) or `false`. |

-----

<!--
### cdc-sink

Use the `cdc-sink` resource to create, update, and delete CDC sinks.

```text
Usage: ybm cdc-sink [command] [flags]
```

Examples:

- Create a CDC sink:

  ```sh
  ybm cdc_sink create \
      --name=sink-2 \
      --hostname=kafka.self.us \
      --auth-type=BASIC \
      --cdc-sink-type=KAFKA \
      --username=admin \
      --password=password
  ```

### create

--name=_name_
: Name for the sink.

--hostname=_host_
: Hostname of the CDC sink.

--auth-type=_authorization_
: Authorization type of the sink. `basic`

--cdc-sink-type=_type_
: Type of CDC sink.

--username=_name_
: Sink user name.

--password=_password_
: Sink user password.

### list

--name=_name_
: Name of the sink.

### update

--name=_name_
: Name of the sink.

--new-name=_name_
: New name for the sink.

--username=_name_
: Sink user name.

--password=_password_
: Sink user password.

### delete

--name=_name_
: Name of the sink.

-----

### cdc-stream

Use the `cdc-stream` resource to create, update, and delete CDC streams.

```text
Usage: ybm cdc-stream [command] [flags]
```

Examples:

- Create a CDC stream:

  ```sh
  ybm cdc-stream create \
      --cluster-name=cluster-1 \
      --name=stream-2 \
      --tables=table1,table2 \
      --sink=mysink \
      --db-name=mydatabase \
      --snapshot-existing-data=true \
      --kafka-prefix=prefix
  ```

### create

--cluster-name=_name_
: Name of the cluster with the tables you want to stream.

--name=_name_
: Name for the stream.

--tables=_table names_
: List of tables the CDC stream will listen to.

--sink=_sink_
: Destination sink for the stream.

--db-name=_database name_
: Database that the Cdc Stream will listen to.

--snapshot-existing-data=_bool_
: Whether to snapshot the existing data in the database.

--kafka-prefix=_prefix_
: Prefix for the Kafka topics.

### list

--cluster-name=_name_
: Name of the cluster with the streams you want to fetch.

--name=_name_
: Name of the CDC stream.

### update

--cluster-name=_name_
: Name of the cluster with the tables you want to stream.

--name=_name_
: Name of the stream.

--tables=_table names_
: List of tables the CDC stream will listen to.

--new-name=_name_
: New name for the stream.

### delete

--cluster-name=_name_
: Name of the cluster with the stream to delete.

--name=_name_
: Name of the stream.

----->
