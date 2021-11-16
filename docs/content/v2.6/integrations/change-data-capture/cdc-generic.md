<!--
---
title: Change Data Capture
headerTitle: Change Data Capture
linkTitle: Change Data Capture
description: Use a local YugabyteDB cluster to stream data changes to stdout using the Change Data Capture API.
beta: /latest/faq/general/#what-is-the-definition-of-the-beta-feature-tag
menu:
  v2.6:
    identifier: change-data-capture-1-cdc-generic
    parent: integrations
    weight: 610
isTocNested: true
showAsideToc: true
---
-->

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="{{< relref "./cdc-generic.md" >}}" class="nav-link active">
      Generic
    </a>
  </li>
  <li >
    <a href="{{< relref "./cdc-kafka.md" >}}" class="nav-link">
      Kafka
    </a>
  </li>

</ul>

[Change data capture (CDC)](../../../architecture/docdb-replication/change-data-capture/) can be used to asynchronously stream data changes from a YugabyteDB cluster to external systems such as message queues and OLAP warehouses. The data changes in YugabyteDB are detected, captured, and then directed to a specified target.

This document demonstrates how to use CDC in conjunction with the [yugabyted](../../../reference/configuration/yugabyted) cluster management utility.

## 1. Create a universe

You can create a Yugabyte universe by executing the following command:

```sh
$ ./bin/yugabyted start
```

## 2. Add a database table

Start your local YugabyteDB cluster and run `ysqlsh` to connect to the service, as follows:

```sh
$ ./bin/ysqlsh
```

Add a table named `products` to the default `yugabyte` database by executing the following YSQL statement:

```sql
CREATE TABLE products(
  id         bigserial PRIMARY KEY,
  created_at timestamp,
  category   text,
  ean        text,
  price      float,
  quantity   int default(5000),
  rating     float,
  title      text,
  vendor     text
);
```

## 3. Download the CDC connector for stdout

Execute the following command to download the stdout CDC connector JAR file:

```sh
$ wget https://downloads.yugabyte.com/yb-cdc-connector.jar
```

## 4. Stream the log output to stdout

Execute the following command to start logging an output stream of data changes from the YugabyteDB `cdc` table to `stdout`:

```sh
java -jar yb-cdc-connector.jar --table_name yugabyte.products
```

- *table_name* specifies the namespace and table, where namespace is the database (YSQL) or keyspace (YCQL).
- *master_addrs* specifies the IP addresses for all of the YB-Master servers that are producing or consuming. The default value is `127.0.0.1:7100`. If you are using a three-node local cluster, then you need to specify a comma-delimited list of the addresses for all of your YB-Master servers.

## 5. Insert values and observe

In another terminal shell, write values, such as the following, to the table, and then observe the values on your `stdout` output stream:

```sql
INSERT INTO products (
  id,
  category,
  created_at,
  ean,
  price,
  rating,
  title,
  vendor)
VALUES (
  14,
  'Widget',
  '2017-12-31T14:41:56.870Z',
  8833419218504,
  25.09876359271891,
  4.0,
  'Awesome Concrete Shoes',
  'McClure-Lockman');
```

```output
2020-01-16 14:52:01,597 [INFO|org.yb.cdc.LogClient|LogClient] time: 6468465138045198336
operation: WRITE
key {
  key: "id"
  value {
    int64_value: 14
  }
}
changes {
  key: "created_at"
  value {
    int64_value: 568046516870000
  }
}
changes {
  key: "category"
  value {
    string_value: "Widget"
  }
}
changes {
  key: "ean"
  value {
    string_value: "8833419218504"
  }
}
changes {
  key: "price"
  value {
    double_value: 25.09876359271891
  }
}
changes {
  key: "quantity"
  value {
    int32_value: 5000
  }
}
changes {
  key: "rating"
  value {
    double_value: 4.0
  }
}
changes {
  key: "title"
  value {
    string_value: "Awesome Concrete Shoes"
  }
}
changes {
  key: "vendor"
  value {
    string_value: "McClure-Lockman"
  }
}
```

## 6. Clean up (optional)

Optionally, you can shut down the local cluster by executing the following command:

```sh
$ ./bin/yugabyted destroy
```
