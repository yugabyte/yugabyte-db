---
title: Integrations
headerTitle: Integrations
linkTitle: Integrations
description: Integrate popular third party tools with YugabyteDB, including Presto, Prisma, Sequelize, Spring, Flyway, Django, Hasura, Kafka.
headcontent: Use YugabyteDB with popular third-party integrations
image: /images/section_icons/develop/api-icon.png
type: indexpage
showRightNav: true
---

Because YugabyteDB is wire compatible with PostgreSQL, YugabyteDB supports many third-party integrations out of the box.

## Choose your integration

### Drivers and ORMs

{{< readfile "../drivers-orms/include-drivers-orms-list.md" >}}

### Schema migration

| Tool | Support | Tutorial |
| :--- | :--- | :--- |
| Flyway    | Full | [Flyway](flyway/) |
| Liquibase | Full | [Liquibase](liquibase/) |
| Prisma    | Full | [Prisma](prisma/) |
| Schema Evolution Manager | Full | [Schema Evolution Manager](schema-evolution-mgr/) |

### Data migration

| Tool | Support | Tutorial |
| :--- | :--- | :--- |
| PGmigrate | Full | [PGmigrate](pgmigrate/) |
| YSQL Loader (pgloader) | Full | [YSQL Loader](ysql-loader/) |

### Data integration (CDC)

| Tool | Support | Tutorial |
| :--- | :--- | :--- |
| Apache Beam    | Full | [Apache Beam](apache-beam/) |
| Apache Flink   | Full | [Apache Flink](apache-flink/) |
| Apache Kafka   | Full | [Apache Kafka](apache-kafka/) |
| Akka Persistence | Full | [Akka Persistence](akka-ysql/) |
| Confluent      | Full | [Confluent Cloud](../explore/change-data-capture/cdc-tutorials/cdc-confluent-cloud/) |
| Debezium       | Full | [Debezium](cdc/debezium/) |
| Hevo Data      | Full | [Hevo Data](hevodata/) |
| Kinesis Data Streams | Full | [Kinesis](kinesis/) |
| RabbitMQ       | Full | [RabbitMQ](rabbitmq/) |
| Synapse        | Full | [Synapse](../explore/change-data-capture/cdc-tutorials/cdc-azure-event-hub/) |

### GUI clients

| Tool | Support | Tutorial |
| :--- | :--- | :--- |
| Apache Superset   | Full | [Apache Superset](../tools/superset/) |
| Arctype   | Full | [Arctype](../tools/arctype/) |
| DBeaver   | Full | [DBeaver](../tools/dbeaver-ysql/) |
| DbSchema  | Full | [DbSchema](../tools/dbschema/) |
| Metabase  | Full | [Metabase](metabase/) |
| pgAdmin   | Full | [pgAdmin](../tools/pgadmin/) |
| SQL Workbench/J | Full | [SQL Workbench/J](../tools/sql-workbench/) |
| TablePlus | Full | [TablePlus](../tools/tableplus/) |

### Application frameworks

| Tool | Support | Tutorial |
| :--- | :--- | :--- |
| AtomicJar Testcontainers | Full | [AtomicJar Testcontainers](atomicjar/) |
| Django | Full | [Django REST Framework](django-rest-framework/) |
| Hasura | Full | [Hasura](hasura/) |
| Spring | Full | [Spring](spring-framework/) |

### Development platforms

| IDE | Support | Tutorial |
| :--- | :--- | :--- |
| Caspio | Full | [Caspio](caspio/) |
| Retool | Full | [Retool](retool/) |
| Superblocks | Full | [Superblocks](superblocks/) |
| Visual Studio Code | Full | [Cassandra Workbench](../tools/visualstudioworkbench/) |

### Data discovery and metadata

| Tool | Support | Tutorial |
| :--- | :--- | :--- |
| Dataedo     | Full | [Dataedo](dataedo/) |
| Datahub     | Full | [Datahub](datahub/) |
| DQ Analyzer | Full | [Ataccama DQ Analyzer](ataccama/) |
| Metacat     | Full | [Metacat](metacat/) |

### Security

| Tool | Support | Tutorial |
| :--- | :--- | :--- |
| Hashicorp Vault      | Full | [Hashicorp Vault](hashicorp-vault/) |
| WSO2 Identity Server | Full | [WSO2 Identity Server](wso2/) |

### Applications powered by YugabyteDB

| Tool | Support | Tutorial |
| :--- | :--- | :--- |
| Camunda | Full | [Camunda](camunda/) |

### Other

| Tool | Latest tested version | Support | Tutorial |
| :--- | :--- | :--- | :--- |
| Apache Atlas |      | Full | [Apache Atlas](atlas-ycql/) |
| Apache Spark | 3.30 | Full | [Apache Spark](apache-spark/) |
| Jaeger       |      | Full | [Jaeger](jaeger/) |
| JanusGraph   |      | Full | [JanusGraph](janusgraph/) |
| KairosDB     |      | Full | [KairosDB](kairosdb/) |
| Mirantis MKE |      | Full | [Mirantis](mirantis/) |
| Presto      | Full | [Presto](presto/) |

<ul class="nav yb-pills">

  <li>
    <a href="akka-ysql/">
      <img src="/images/section_icons/develop/ecosystem/akka-icon.png">
      Akka Persistence
    </a>
  </li>

  <li>
    <a href="atlas-ycql/">
      <img src="/images/section_icons/develop/ecosystem/atlas-icon.png">
      Apache Atlas
    </a>
  </li>
  <li>
    <a href="apache-beam/">
      <img src="/images/section_icons/develop/ecosystem/beam.png">
      Apache Beam
    </a>
  </li>
  <li>
    <a href="apache-flink/">
      <img src="/images/section_icons/develop/ecosystem/apache-flink.png">
      Apache Flink
    </a>
  </li>

  <li>
    <a href="apache-kafka/">
      <img src="/images/section_icons/develop/ecosystem/apache-kafka-icon.png">
      Apache Kafka
    </a>
  </li>

  <li>
    <a href="apache-spark/">
      <img src="/images/section_icons/develop/ecosystem/apache-spark.png">
      Apache Spark
    </a>
  </li>

  <li>
    <a href="ataccama/">
      <img src="/images/section_icons/develop/ecosystem/ataccama.png">
      Ataccama DQ Analyzer
    </a>
  </li>

  <li>
    <a href="atomicjar/">
      <img src="/images/section_icons/develop/ecosystem/atomicjar-icon.png">
      AtomicJar Testcontainers
    </a>
  </li>

  <li>
    <a href="camunda/">
      <img src="/images/section_icons/develop/ecosystem/camunda.png">
      Camunda
    </a>
  </li>

  <li>
    <a href="caspio/">
      <img src="/images/section_icons/develop/ecosystem/caspio.png">
      Caspio
    </a>
  </li>

   <li>
    <a href="datahub/">
      <img src="/images/section_icons/develop/ecosystem/datahub.png">
      Datahub
    </a>
  </li>

  <li>
    <a href="dataedo/">
      <img src="/images/section_icons/develop/ecosystem/dataedo.png">
      Dataedo
    </a>
  </li>

  <li>
    <a href="cdc/debezium/">
      <img src="/images/section_icons/develop/ecosystem/debezium.png">
      Debezium
    </a>
  </li>

  <li>
    <a href="django-rest-framework/">
      <img src="/images/section_icons/develop/ecosystem/django-icon.png">
      Django
    </a>
  </li>

  <li>
    <a href="flyway/">
      <img src="/images/section_icons/develop/ecosystem/flyway.png">
      Flyway
    </a>
  </li>

  <li>
    <a href="gorm/">
      <img src="/images/section_icons/develop/ecosystem/gorm-icon.png">
      GORM
    </a>
  </li>

  <li>
    <a href="hashicorp-vault/">
      <img src="/images/section_icons/develop/ecosystem/hashicorp-vault.png">
      Hashicorp Vault
    </a>
  </li>
  <li>
    <a href="hasura/">
      <img src="/images/section_icons/develop/ecosystem/hasura.png">
      Hasura
    </a>
  </li>

   <li>
    <a href="hevodata/">
      <img src="/images/section_icons/develop/ecosystem/hevodata.png">
      Hevo Data
    </a>
  </li>

  <li>
    <a href="jaeger/">
      <img src="/images/section_icons/develop/ecosystem/jaeger.png">
      Jaeger
    </a>
  </li>
  <li>
    <a href="janusgraph/">
      <img src="/images/section_icons/develop/ecosystem/janusgraph.png">
      JanusGraph
    </a>
  </li>

  <li>
    <a href="kairosdb/">
      <img src="/images/section_icons/develop/ecosystem/kairosdb.png">
      KairosDB
    </a>
  </li>

  <li>
    <a href="kinesis/">
      <img src="/images/section_icons/develop/ecosystem/kinesis.png">
      Kinesis Data Streams
    </a>
  </li>

  <li>
    <a href="liquibase/">
      <img src="/images/section_icons/develop/ecosystem/liquibase.png">
      Liquibase
    </a>
  </li>

  <li>
    <a href="metabase/">
      <img src="/images/section_icons/develop/ecosystem/metabase.png">
      Metabase
    </a>
  </li>

  <li>
    <a href="metacat/">
      <img src="/images/section_icons/develop/ecosystem/metacat.png">
      Metacat
    </a>
  </li>

   <li>
    <a href="mirantis/">
      <img src="/images/section_icons/develop/ecosystem/mirantis.png">
      Mirantis MKE
    </a>
  </li>
   <li>
    <a href="pgmigrate/">
      <img src="/images/section_icons/develop/ecosystem/pgmigrate.png">
      PGmigrate
    </a>
  </li>
  <li>
    <a href="presto/">
      <img src="/images/section_icons/develop/ecosystem/presto-icon.png">
      Presto
    </a>
  </li>

  <li>
    <a href="prisma/">
      <img src="/images/develop/graphql/prisma/prisma.png">
      Prisma
    </a>
  </li>

  <li>
    <a href="rabbitmq/">
      <img src="/images/section_icons/develop/ecosystem/rabbitmq.png">
      RabbitMQ
    </a>
  </li>

  <li>
    <a href="retool/">
      <img src="/images/section_icons/develop/ecosystem/retool.png">
      Retool
    </a>
  </li>

  <li>
    <a href="schema-evolution-mgr/">
      Schema Evolution Manager
    </a>
  </li>

  <li>
    <a href="sequelize/">
      <img src="/images/section_icons/develop/ecosystem/sequelize.png">
      Sequelize
    </a>
  </li>

  <li>
    <a href="spring-framework/">
      <img src="/images/section_icons/develop/ecosystem/spring.png">
      Spring
    </a>
  </li>

  <li>
    <a href="sqlalchemy/">
      <img src="/images/section_icons/develop/ecosystem/sqlalchemy.png">
      SQLAlchemy
    </a>
  </li>

  <li>
    <a href="superblocks/">
      <img src="/images/section_icons/develop/ecosystem/superblocks.png">
      Superblocks
    </a>
  </li>

  <li>
    <a href="wso2/">
      <img src="/images/section_icons/develop/ecosystem/wso2.png">
      WSO2 Identity Server
    </a>
  </li>

  <li>
    <a href="ysql-loader/">
      <i class="icon-postgres"></i>
      YSQL Loader
    </a>
  </li>

</ul>
