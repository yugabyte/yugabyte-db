---
title: Integrations
headerTitle: Integrations
linkTitle: Integrations
description: Integrate popular third party tools with YugabyteDB, including Presto, Prisma, Sequelize, Spring, Flyway, Django, Hasura, Kafka.
headcontent: Use YugabyteDB with popular third-party integrations
image: /images/section_icons/develop/api-icon.png
type: indexpage
cascade:
  unversioned: true
showRightNav: true
---

Because YugabyteDB is wire compatible with PostgreSQL, most PostgreSQL client drivers, ORM frameworks, and other types of third-party database tools designed for PostgreSQL are compatible with YugabyteDB.

Yugabyte has partnered with open-source projects and vendors to support the following popular PostgreSQL tools.

| Support | Description |
| :--- | :--- |
| Full    | Compatibility with the vast majority of the tool or driver features is maintained. These tools are regularly tested against the latest version documented. |
| Partial | Core functions, such as connecting and performing basic database operations, are compatible with YugabyteDB. Full integration may require additional steps, and some features may not be supported or exhibit unexpected behavior. These tools will eventually have full support from YugabyteDB. |

Version refers to the latest tested version of the integration.

## Choose your integration

### Drivers and ORMs

{{< readfile "../drivers-orms/include-drivers-orms-list.md" >}}

### Schema migration

| Tool      | Version | Support | Tutorial |
| :-------- | :------ | :------ | :------- |
| Liquibase | 4.23.1  | Full    | [Liquibase](liquibase/) |
| Flyway    | 7.11.2  | Partial | [Flyway](flyway/) |
| Prisma    | 5.1.0   | Full    | [Prisma](prisma/) |
| Schema Evolution Manager | 0.9.47 | Partial | [Schema Evolution Manager](schema-evolution-mgr/) |

### Data migration

| Tool      | Version | Support | Tutorial |
| :---------| :------ | :------ | :------- |
| PGmigrate | 1.0.7   | Partial | [PGmigrate](pgmigrate/) |
| YSQL Loader (pgloader) | 3.6.3 | Full | [YSQL Loader](ysql-loader/) |

### Data integration (CDC)

| Tool           | Version | Support | Tutorial |
| :------------- | :------ | :------ | :------- |
| Apache Beam    | 2.49.0  | Partial | [Apache Beam](apache-beam/) |
| Apache Flink   | 1.17.1  | Partial | [Apache Flink](apache-flink/) |
| Akka Persistence | 1.0.1 | Partial | [Akka Persistence](akka-ysql/) |
| Confluent      | 7.4.0   | Full | [Confluent Cloud](../tutorials/cdc-tutorials/cdc-confluent-cloud/) |
| Debezium       | 1.9.5   | Full | [Debezium](cdc/debezium/) |
| Hevo Data      | 1.66    | Partial | [Hevo Data](hevodata/) |
| Kinesis Data Streams |   | Full | [Kinesis](kinesis/) |
| RabbitMQ       | 3.11.21 | Partial | [RabbitMQ](rabbitmq/) |
| Synapse        |         | Full | [Synapse](../tutorials/cdc-tutorials/cdc-azure-event-hub/) |

### GUI clients

| Tool            | Version | Support | Tutorial |
| :-------------- | :------ | :------ | :------- |
| Apache Superset |         | Full    | [Apache Superset](../tools/superset/) |
| Arctype         |         | Full    | [Arctype](../tools/arctype/) |
| DataGrip        | 2023.2.2 | Full   |  |
| DBeaver         | 23.2.2  | Full    | [DBeaver](../tools/dbeaver-ysql/) |
| DbSchema        |         | Full    | [DbSchema](../tools/dbschema/) |
| Metabase        |         | Full    | [Metabase](../tools/metabase/) |
| pgAdmin         |         | Full    | [pgAdmin](../tools/pgadmin/) |
| SQL Workbench/J |         | Full    | [SQL Workbench/J](../tools/sql-workbench/) |
| TablePlus       | 2.18    | Full    | [TablePlus](../tools/tableplus/) |

### Application frameworks

| Tool        |      | Version      | Support | Tutorial |
| :---------- | :--- | :----------- | :------ | :------- |
| AtomicJar Testcontainers | | | Partial | [AtomicJar Testcontainers](atomicjar/) |
| Django | | 3.2 or later | Full | [Django REST Framework](django-rest-framework/) |
| Hasura | | 1.3.3        | Full | [Hasura](hasura/) |
| Spring | Spring Data YugabyteDB | 2.3.0          | Full | [Spring](spring-framework/) |
|        | Spring Data JPA        | 2.6.3          | Full |  |
|        | Spring Data Cassandra  | 2.2.12 / 3.06  | Full |  |

### Development platforms

| IDE         | Version | Support | Tutorial |
| :---------- | :------ | :------ | :------- |
| Budibase    |         | Partial | [Budibase](budibase/) |
| Caspio      |         | Partial | [Caspio](caspio/) |
| Retool      | 3.1     | Partial | [Retool](retool/) |
| Superblocks |         | Partial | [Superblocks](superblocks/) |
<!-- tool not maintained | Visual Studio Code |  | Partial | [Cassandra Workbench](../tools/visualstudioworkbench/) | -->

### Data discovery and metadata

| Tool        | Version      | Support | Tutorial |
| :---------- | :----------- | :------ | :------- |
| Dataedo     | 23.1.1       | Partial | [Dataedo](dataedo/) |
| Datahub     | 0.10.4       | Partial | [Datahub](datahub/) |
| DQ Analyzer | 11.1.1       | Partial | [Ataccama DQ Analyzer](ataccama/) |
| Metacat     | 1.3.0-rc.105 | Partial | [Metacat](metacat/) |

### Security

| Tool    | Version | Support | Tutorial |
| :------ | :------ | :------ | :------- |
| Hashicorp Vault      | 1.0.0  | Full | [Hashicorp Vault](hashicorp-vault/) |
| WSO2 Identity Server | 5.11.0 | Full | [WSO2 Identity Server](wso2/) |

### Applications powered by YugabyteDB

| Tool    | Version | Support | Tutorial |
| :------ | :------ | :------ | :------- |
| Camunda | 7       | Partial | [Camunda](camunda/) |

### Hardware, Software, and Data Management platforms

| Tool        | Tutorial |
| :------     | :------- |
| Nutanix AHV | [Nutanix AHV](nutanix-ahv/) |
| Commvault   | [Commvault](commvault/) |
| Delphix     | [Delphix](delphix/) |

### Other

| Tool         | Version | Support | Tutorial |
| :----------- | :------ | :------ | :------- |
| Apache Atlas | 2.3.0   | Partial | [Apache Atlas](atlas-ycql/) |
| Apache Hudi  | 0.14.1  | Full    | [Apache Hudi](https://www.yugabyte.com/blog/apache-hudi-data-lakehouse-integration/) |
| Apache Spark | 3.3.0   | Full    | [Apache Spark](apache-spark/) |
| Jaeger       | 1.43.0  | Full    | [Jaeger](jaeger/) |
| JanusGraph   | 0.6.2   | Full    | [JanusGraph](janusgraph/) |
| KairosDB     | 1.3.0   | Full    | [KairosDB](kairosdb/) |
| Mirantis MKE | 3.5.8   | Partial | [Mirantis](mirantis/) |
| Presto       | 309     | Partial | [Presto](presto/) |

<!--
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
    <a href="">
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
-->
