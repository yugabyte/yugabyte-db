---
title: Apache Flink
linkTitle: Apache Flink
description: Apache Flink
menu:
  preview_integrations:
    identifier: apache-flink
    parent: data-integration
    weight: 571
type: docs
---

[Apache Flink](https://flink.apache.org/) is a framework and distributed processing engine for stateful computations over unbounded and bounded data streams.

Flink provides various DataStream connectors including one for JDBC to write data to various databases. You can use this connector to write date from Flink to YugabyteDB.

## Connect

The following example based on the [Flink documentation](https://nightlies.apache.org/flink/flink-docs-release-1.17/docs/connectors/datastream/jdbc) describes the Flink connectivity to a YugabyteDB cluster using a JDBC sink connector.

Your YugabyteDB cluster should be up and running. Refer to [YugabyteDB Prerequisites](../../tools/#yugabytedb-prerequisites).
Note that the YugabyteDB cluster you are connecting to is running on localhost.

Create a table named books using [YSQLSH](../../admin/ysqlsh/#starting-ysqlsh) as follows:

```sql
create table books (id bigint, title varchar, authors varchar, year int);
```

Compile and run the following program:

```java
package com.yugabyte;

import org.apache.flink.connector.jdbc.JdbcConnectionOptions;
import org.apache.flink.connector.jdbc.JdbcExecutionOptions;
import org.apache.flink.connector.jdbc.JdbcSink;
import org.apache.flink.streaming.api.environment.StreamExecutionEnvironment;

public class JdbcSinkExample {

   static class Book {
       public Book(Long id, String title, String authors, Integer year) {
           this.id = id;
           this.title = title;
           this.authors = authors;
           this.year = year;
       }
       final Long id;
       final String title;
       final String authors;
       final Integer year;
   }

   public static void main(String[] args) throws Exception {
       StreamExecutionEnvironment env = StreamExecutionEnvironment.getExecutionEnvironment();

       env.fromElements(
               new Book(101L, "Stream Processing with Apache Flink", "Fabian Hueske, Vasiliki Kalavri", 2019),
               new Book(102L, "Streaming Systems", "Tyler Akidau, Slava Chernyak, Reuven Lax", 2018),
               new Book(103L, "Designing Data-Intensive Applications", "Martin Kleppmann", 2017),
               new Book(104L, "Kafka: The Definitive Guide", "Gwen Shapira, Neha Narkhede, Todd Palino", 2017)
       ).addSink(
               JdbcSink.sink(
                       "insert into books (id, title, authors, year) values (?, ?, ?, ?)",
                       (statement, book) -> {
                           statement.setLong(1, book.id);
                           statement.setString(2, book.title);
                           statement.setString(3, book.authors);
                           statement.setInt(4, book.year);
                       },
                       JdbcExecutionOptions.builder()
                               .withBatchSize(1000)
                               .withBatchIntervalMs(200)
                               .withMaxRetries(5)
                               .build(),
                       new JdbcConnectionOptions.JdbcConnectionOptionsBuilder()
                               .withUrl("jdbc:postgresql://localhost:5433/yugabyte")
                               .withDriverName("org.postgresql.Driver")
                               .withUsername("yugabyte")
                               .withPassword("yugabyte")
                               .build()
               ));

       env.execute();
       System.out.println("Done");
   }
}
```
