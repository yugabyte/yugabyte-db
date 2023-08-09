---
title: Use RabbitMQ with YugabyteDB YSQL
headerTitle: RabbitMQ
linkTitle: RabbitMQ
description: Use RabbitMQ to work with distributed SQL databases in YugabyteDB.
menu:
  preview_integrations:
    identifier: rabbitmq
    parent: integrations
    weight: 571
type: docs
---

[RabbitMQ](https://www.rabbitmq.com/) is one of the most popular open source message brokers. RabbitMQ is lightweight and easy to deploy on-premises and in the cloud. It supports multiple messaging protocols and [streaming](https://www.rabbitmq.com/streams.html). RabbitMQ can be deployed in distributed and federated configurations to meet high-scale, high-availability requirements.

## Prerequisites

To use RabbitMQ, ensure that you have the following:

- YugabyteDB up and running. Download and install YugabyteDB by following the steps in [Quick start](../../quick-start/).
- [RabbitMQ](https://www.rabbitmq.com/download.html) installed and running.
- IntelliJ Idea CE

## Setup

To run RabbitMQ with YugabyteDB, do the following:

1. From your YugabyteDB home directory, run the following command to generate a stream ID:

    ```sh
    ./bin/yb-admin --master_addresses 127.0.0.1:7100 create_change_data_stream ysql.yugabyte
    ```

    You can see output similar to the following:

    ```output
    CDC Stream ID: 31dd4440caca46038ba4a9365bb89d36
    ```

1. Create a table from your [ysql shell](../../admin/ysqlsh/#starting-ysqlsh) using the following command:

    ```sh
    create table test (id int primary key, name text, days_worked bigint);
    ```

1. Create a maven project in IntelliJ.
1. Download the [client library](https://repo1.maven.org/maven2/com/rabbitmq/amqp-client/5.16.0/amqp-client-5.16.0.jar) and its dependencies ([SLF4J API](https://repo1.maven.org/maven2/org/slf4j/slf4j-api/1.7.36/slf4j-api-1.7.36.jar) and [SLF4J Simple](https://repo1.maven.org/maven2/org/slf4j/slf4j-simple/1.7.36/slf4j-simple-1.7.36.jar)), and copy those files in your working directory.
1. Add the following in your pom.xml file:

    ```xml
    <?xml version="1.0" encoding="UTF-8"?>
    <project xmlns="http://maven.apache.org/POM/4.0.0" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
      xsi:schemaLocation="http://maven.apache.org/POM/4.0.0 http://maven.apache.org/xsd/maven-4.0.0.xsd">
      <modelVersion>4.0.0</modelVersion>
      <groupId>com.dbzapp</groupId>
      <artifactId>dbz-embedded-yb-app</artifactId>
      <version>0.1-SNAPSHOT</version>
      <name>dbz-embedded-yb-app</name>
      <properties>
        <project.build.sourceEncoding>UTF-8</project.build.sourceEncoding>
        <maven.compiler.source>11</maven.compiler.source>
        <maven.compiler.target>11</maven.compiler.target>
        <version.surefire.plugin>3.0.0-M3</version.surefire.plugin>
        <version.failsafe.plugin>${version.surefire.plugin}</version.failsafe.plugin>
        <version.compiler.plugin>3.8.1</version.compiler.plugin>
        <version.assembly.plugin>3.4.1</version.assembly.plugin>
        <version.release.plugin>3.0.0-M5</version.release.plugin>
        <version.resources.plugin>3.1.0</version.resources.plugin>
        <version.s3.wagon>0.1.3</version.s3.wagon>
        <version.debezium>1.7.0.Final</version.debezium>
        <version.debezium.yb>1.7.0.11-BETA</version.debezium.yb>
      </properties>
      <repositories>
        <repository>
            <id>maven.release.yugabyte.repo</id>
            <url>s3://repository.yugabyte.com/maven/release</url>
        </repository>
        <repository>
          <id>confluent</id>
          <url>https://packages.confluent.io/maven/</url>
        </repository>
        <repository>
          <id>maven.yugabyte.repo</id>
          <url>s3://repository.yugabyte.com/maven</url>
          <releases>
            <enabled>true</enabled>
            <updatePolicy>never</updatePolicy>
          </releases>
        </repository>
      </repositories>
      <pluginRepositories>
        <pluginRepository>
          <snapshots>
            <enabled>false</enabled>
          </snapshots>
          <id>central</id>
          <name>Central Repository</name>
          <url>https://repo.maven.apache.org/maven2</url>
        </pluginRepository>
      </pluginRepositories>
      <distributionManagement>
        <site>
            <id>s3.site</id>
            <url>s3://repository.yugabyte.com/maven/site</url>
        </site>
        <repository>
            <id>s3.release</id>
            <url>s3://repository.yugabyte.com/maven/release</url>
        </repository>
        <snapshotRepository>
          <id>s3.snapshot</id>
          <url>s3://repository.yugabyte.com/snapshot</url>
        </snapshotRepository>
      </distributionManagement>
      <dependencies>
        <dependency>
            <groupId>commons-cli</groupId>
            <artifactId>commons-cli</artifactId>
            <version>1.5.0</version>
        </dependency>
        <dependency>
            <groupId>io.debezium</groupId>
            <artifactId>debezium-api</artifactId>
            <version>${version.debezium}</version>
        </dependency>
        <dependency>
            <groupId>io.debezium</groupId>
            <artifactId>debezium-embedded</artifactId>
            <version>${version.debezium}</version>
        </dependency>
        <dependency>
            <groupId>org.yb</groupId>
            <artifactId>debezium-connector-yugabytedb</artifactId>
            <version>${version.debezium.yb}</version>
        </dependency>
        <dependency>
          <groupId>junit</groupId>
          <artifactId>junit</artifactId>
          <version>4.11</version>
          <scope>test</scope>
        </dependency>
        <dependency>
          <groupId>com.rabbitmq</groupId>
          <artifactId>amqp-client</artifactId>
          <version>5.14.2</version>
        </dependency>
      </dependencies>
      <build>
        <extensions>
          <extension>
            <groupId>com.yugabyte</groupId>
            <artifactId>maven-s3-wagon</artifactId>
            <version>${version.s3.wagon}</version>
          </extension>
        </extensions>
        <plugins>
          <plugin>
            <groupId>org.apache.maven.plugins</groupId>
            <artifactId>maven-assembly-plugin</artifactId>
            <version>${version.assembly.plugin}</version>
            <configuration>
              <finalName>dbz-embedded-yb-app</finalName>
              <appendAssemblyId>false</appendAssemblyId>
              <archive>
                <manifest>
                  <addClasspath>true</addClasspath>
                  <mainClass>com.dbzapp.App</mainClass>
                </manifest>
              </archive>
              <descriptorRefs>
                <descriptorRef>jar-with-dependencies</descriptorRef>
              </descriptorRefs>
            </configuration>
            <executions>
              <execution>
                <id>make-assembly</id>
                <phase>package</phase>
                <goals>
                    <goal>single</goal>
                </goals>
              </execution>
            </executions>
          </plugin>
          <plugin>
            <groupId>org.apache.maven.plugins</groupId>
            <artifactId>maven-compiler-plugin</artifactId>
            <version>${version.compiler.plugin}</version>
            <configuration>
              <source>${maven.compiler.source}</source>
              <target>${maven.compiler.target}</target>
              <encoding>${project.build.sourceEncoding}</encoding>
            </configuration>
          </plugin>
          <plugin>
            <groupId>org.apache.maven.plugins</groupId>
            <artifactId>maven-resources-plugin</artifactId>
            <version>${version.resources.plugin}</version>
            <configuration>
              <encoding>${project.build.sourceEncoding}</encoding>
            </configuration>
          </plugin>
          <plugin>
            <groupId>org.apache.maven.plugins</groupId>
            <artifactId>maven-failsafe-plugin</artifactId>
            <version>${version.failsafe.plugin}</version>
            <executions>
              <execution>
                <id>integration-test</id>
                <goals>
                  <goal>integration-test</goal>
                </goals>
              </execution>
              <execution>
                <id>verify</id>
                <goals>
                  <goal>verify</goal>
                </goals>
              </execution>
            </executions>
            <configuration>
              <skipTests>${skipITs}</skipTests>
              <enableAssertions>true</enableAssertions>
            </configuration>
          </plugin>
        </plugins>
      </build>
      <profiles>
        <profile>
          <id>quick</id>
          <activation>
            <activeByDefault>false</activeByDefault>
            <property>
              <name>quick</name>
            </property>
          </activation>
          <properties>
            <skipITs>true</skipITs>
            <maven.test.skip>true</maven.test.skip>
          </properties>
        </profile>
      </profiles>
    </project>
    ```

1. Create a `App.java` class file in the `src` folder, and add the following code to it. This is the entry point for your application to run:

    ```java
    public class App {
      public static void main(String[] args) {
        System.out.println("Starting embedded application to run DBZ Embedded engine with Yugabyte");
        CmdLineOpts configuration = CmdLineOpts.createFromArgs(args);
        try {
          EngineRunner engineRunner = new EngineRunner(configuration);
          engineRunner.run();
        } catch (Exception e) {
          System.out.println("Exception while trying to run the engine: " + e);
          System.exit(-1);
        }
      }
    }
    ```

1. Create a configuration file `CmdLineOpts.java` in the `src` folder, where you can store the connection properties to connect to YugabyteDB. Add the following code to it. Set the "streamId" to the stream ID generated in step 1:

    ```java

    package com.dbzapp;
    import java.util.Properties;
    import org.apache.commons.cli.CommandLine;
    import org.apache.commons.cli.CommandLineParser;
    import org.apache.commons.cli.DefaultParser;
    import org.apache.commons.cli.Options;
    /**
     * Helper class to parse the command line options.
     *
     * @author Sumukh Phalgaonkar, Vaibhav Kushwaha (vkushwaha@yugabyte.com)
     */
    public class CmdLineOpts {
      private final String connectorClass = "io.debezium.connector.yugabytedb.YugabyteDBConnector";
      public String masterAddresses;
      public String hostname;
      public String databasePort = "5433";
      public String streamId;
      public String tableIncludeList;
      public String databaseName = "yugabyte";
      public String databasePassword = "yugabyte";
      public String databaseUser = "yugabyte";
      public String snapshotMode = "never";

      public static CmdLineOpts createFromArgs(String[] args) {
        Options options = new Options();
        options.addOption("master_addresses", true, "Addresses of the master process");
        options.addOption("stream_id", true, "DB stream ID");
        options.addOption("table_include_list", true, "The table list to poll for in the form"
                          + " <schemaName>.<tableName>");
        CommandLineParser parser = new DefaultParser();
        CommandLine commandLine = null;
        try {
          commandLine = parser.parse(options, args);
        } catch (Exception e) {
          System.out.println("Exception while parsing arguments: " + e);
          System.exit(-1);
        }
        CmdLineOpts configuration = new CmdLineOpts();
        configuration.initialize(commandLine);
        return configuration;
      }
      private void initialize(CommandLine commandLine) {
          masterAddresses = "127.0.0.1:7100";
          String[] nodes = masterAddresses.split(",");
          hostname = nodes[0].split(":")[0];

          streamId = "31dd4440caca46038ba4a9365bb89d36";

          tableIncludeList = "public.test";

      }
      public Properties asProperties() {
        Properties props = new Properties();
        props.setProperty("connector.class", connectorClass);
        props.setProperty("database.streamid", streamId);
        props.setProperty("database.master.addresses", masterAddresses);
        props.setProperty("table.include.list", tableIncludeList);
        props.setProperty("database.hostname", hostname);
        props.setProperty("database.port", databasePort);
        props.setProperty("database.user", databaseUser);
        props.setProperty("database.password", databasePassword);
        props.setProperty("database.dbname", databaseName);
        props.setProperty("database.server.name", "dbserver1");
        props.setProperty("snapshot.mode", snapshotMode);
        return props;
      }
    }
    ```

1. Create a `Send.java` class in the `src` folder. This creates a channel between the producer and the queue and then publishes the message to the queue:

    ```java

    package com.dbzapp;
    import com.rabbitmq.client.Channel;
    import com.rabbitmq.client.Connection;
    import com.rabbitmq.client.ConnectionFactory;
    import java.nio.charset.StandardCharsets;
    public class Send {
        private final static String QUEUE_NAME = "hello";
        public static void run(String msg) throws Exception {
            ConnectionFactory factory = new ConnectionFactory();
            factory.setHost("localhost");
            try (Connection connection = factory.newConnection();
                 Channel channel = connection.createChannel()) {
                channel.queueDeclare(QUEUE_NAME, false, false, false, null);
                String message = msg;
                channel.basicPublish("", QUEUE_NAME, null, message.getBytes(StandardCharsets.UTF_8));
                System.out.println(" [x] Sent '" + message + "'");
            }
        }
    }
    ```

1. Create an `EngineRunner.java` class in the `src` folder, which starts the Debezium engine and captures the changes on the server side as records. It then calls the `Send.java` class run API with the record that it received to publish it to the queue.

    ```java

    package com.dbzapp;
    import io.debezium.connector.yugabytedb.*;
    import io.debezium.engine.ChangeEvent;
    import io.debezium.engine.DebeziumEngine;
    import io.debezium.engine.format.Json;
    import org.apache.kafka.connect.json.*;
    import java.util.Properties;
    import java.util.concurrent.ExecutorService;
    import java.util.concurrent.Executors;

    public class EngineRunner {
      private CmdLineOpts config;
      public EngineRunner(CmdLineOpts config) {
        this.config = config;
      }
      public void run() throws Exception {
        final Properties props = config.asProperties();
        props.setProperty("name", "engine");
        props.setProperty("offset.storage", "org.apache.kafka.connect.storage.FileOffsetBackingStore");
        props.setProperty("offset.storage.file.filename", "/tmp/offsets.dat");
        props.setProperty("offset.flush.interval.ms", "60000");
        Send obj = new Send();

        // Create the engine with this configuration ...
        try (DebeziumEngine<ChangeEvent<String, String>> engine = DebeziumEngine.create(Json.class)
                .using(props)
                .notifying(record -> {
                  try {
                    obj.run(record.toString());
                  } catch (Exception e) {
                    e.printStackTrace();
                  }
                  System.out.println(record);
                }).build()
            ) {
          // Run the engine asynchronously ...
          ExecutorService executor = Executors.newSingleThreadExecutor();
          executor.execute(engine);
        } catch (Exception e) {
          System.out.println(e);
          throw e;
        }
      }
    }
    ```

1. Finally, create a class `Recv.java` in the `src` folder which consumes the records from the queue and outputs to the console:

    ```java

    package com.dbzapp;
    import com.rabbitmq.client.Channel;
    import com.rabbitmq.client.Connection;
    import com.rabbitmq.client.ConnectionFactory;
    import com.rabbitmq.client.DeliverCallback;
    import java.nio.charset.StandardCharsets;
    public class Recv {
        private final static String QUEUE_NAME = "hello";
        public static void main(String[] argv) throws Exception {
            ConnectionFactory factory = new ConnectionFactory();
            factory.setHost("localhost");
            Connection connection = factory.newConnection();
            Channel channel = connection.createChannel();
            channel.queueDeclare(QUEUE_NAME, false, false, false, null);
            System.out.println(" [*] Waiting for messages. To exit press CTRL+C");
            DeliverCallback deliverCallback = (consumerTag, delivery) -> {
                String message = new String(delivery.getBody(), StandardCharsets.UTF_8);
                System.out.println(" [x] Received '" + message + "'");
            };
            channel.basicConsume(QUEUE_NAME, true, deliverCallback, consumerTag -> { });
        }
    }
    ```

1. Run the `Recv.java` class for the receiver to start listening. You can see output similar to the following:

    ```output
    [*] Waiting for messages. To exit press CTRL+C
    ```

1. From a new terminal run the `App.java` class.
1. After both the applications are running, open the ysqlsh shell and insert a row into the test table as follows:

    ```sql
    INSERT into test values (1, Jake, 20);
    ```

1. From the terminal where `App.java` runs, you can see a log output similar to the following:

    ```output
    [x] Sent 'EmbeddedEngineChangeEvent [key={"schema":{"type":"struct","fields":[{"type":"struct","fields":[{"type":"int32","optional":false,"field":"value"},{"type":"boolean","optional":false,"field":"set"}],"optional":false,"name":"id","field":"id"}],"optional":false,"name":"dbserver1.public.test.Key"},"payload":{"id":{"value":8,"set":true}}}
    .
    .
    .
    .
    .
    valueSchema=Schema{dbserver1.public.test.Envelope:STRUCT}, timestamp=null, headers=ConnectHeaders(headers=)}]
    ```

1. From the terminal where `Recv.java` runs, you can see a log output similar to the following:

    ```output
     [x] Received 'EmbeddedEngineChangeEvent [key={"schema":{"type":"struct","fields":[{"type":"struct","fields":[{"type":"int32","optional":false,"field":"value"},{"type":"boolean","optional":false,"field":"set"}],"optional":false,"name":"id","field":"id"}],"optional":false,"name":"dbserver1.public.test.Key"},"payload":{"id":{"value":8,"set":true}}},
    .
    .
    .
    .
    .
    schema=public,table=test,txId=,lsn=1:6::0:0},op=c,ts_ms=1689163819161}, valueSchema=Schema{dbserver1.public.test.Envelope:STRUCT},     timestamp=null, headers=ConnectHeaders(headers=)}]'
    ```
