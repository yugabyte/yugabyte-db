---
title: Build Python applications using Apache Spark and YugabyteDB YCQL
headerTitle: Build a Python application using Apache Spark and YugabyteDB
linkTitle: YCQL
description: Learn how to build a Python application using Apache Spark and YugabyteDB YCQL
aliases:
  - /preview/integrations/apache-spark/python/
menu:
  preview_integrations:
    identifier: apache-spark-3-python-ycql
    parent: apache-spark
    weight: 576
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../java-ycql/" class="nav-link">
      <i class="fa-brands fa-java" aria-hidden="true"></i>
      Java
    </a>
  </li>

  <li >
    <a href="../scala-ycql/" class="nav-link">
      <i class="icon-scala" aria-hidden="true"></i>
      Scala
    </a>
  </li>

  <li >
    <a href="../python-ycql/" class="nav-link active">
      <i class="icon-python" aria-hidden="true"></i>
      Python
    </a>
  </li>

</ul>

## PySpark

To build your Python application using the YugabyteDB Spark Connector for YCQL, start PySpark with the following for Scala 2.11:

```sh
$ pyspark --packages com.yugabyte.spark:spark-cassandra-connector_2.11:2.4-yb-3
```
