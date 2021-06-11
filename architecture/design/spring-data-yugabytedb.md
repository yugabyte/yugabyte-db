# Spring Data YugabyteDB Design Details

# Motivation

Spring Data modules are one of the widely used Java frameworks for accessing the databases. Spring data provides consistent and familiar APIs for querying the data by supporting Data repository, template methods. For app developers these common set of APIs provided by Spring Data abstracts away the need to learn the database-specific query language, reducing the learning curve required for working with a database.

Spring Data YugabyteDB modules intend to bring support for YugabyteDB YSQL and YCQL API for building modern cloud native applications. For application developers, Spring Data YugabyteDB will bridge the gap for learning the distributed SQL concepts with familiarity and ease of Spring Data APIs.

# Initial Design Goals

### YSQL

1. Spring Data YugabyteDB Template for YSQL
2. Spring Data YugabyteDB repository for YSQL
     a. HASH/ASC/DESC options for pkey/index (@Id) (non-blocker if not programmatically supported)
     b. SPLIT INTO/AT (non-blocker if not programmatically supported)
     c. Maybe a uuid/timeuuid/JSON version for @generated key columns
     d. Read/write Isolation
     e. Read - follower reads
     f. Use YugabyteDB JDBC clusteraware driver
3. Auto reconfiguration support `spring.data.yugabytedb.ysql.*`

### YCQL
1. Spring Data YugabyteDB Template for YCQL 
2. Spring Data repository support for YCQL and YSQL 
     a. @enableYugabyteDBYcqlRepositories
     b. JSONB data type support for YCQL. Native data type supported in YugabyteDB
     c. YugabyteDB YCQL Driver support for PartitionAwarePolicy for One hop fetch
     d. Transactional support in YugabyteDB YCQL repository
     e. Prepared statement support for YCQL repository
3. Auto reconfiguration support `spring.data.yugabytedb.ycql.*`

# Spring Data YugabyteDB

Spring Data YugabyteDB (SDYB) is a top-level project which provides support for both YCQL and YSQL APIs. SDYB will have two projects,

- Spring Data YSQL
- Spring Data YCQL

Spring Data YugabyteDB will have the parent project BOM which will include both these dependencies. 
```
<dependency>
    <groupId>com.yugabyte</groupId>
    <artifactId>spring-data-yugabytedb-dependencies</artifactId>
    <version>2.3.0.RELEASE</version>
</dependency>
```

### Requirements of Spring Data Projects

Spring Data projects should support following database specific configurations,

- Database connectivity
- Externalizing database specific configuration using application.properties
- Database specific data access templates for CRUD operations
- Database specific data access repositories for CRUD operations
- For YCQL -> Implement `@enableyugabyteycqlrepositories` and `@enabletransactionsupport` annotation
- Support prepared statements in YugabyteDB YCQL repository
- Database specific security implementation

## Spring Data YSQL

Spring Data JDBC module provides JDBC template and JDBC repository implementation for relational databases with JDBC access. Spring Data JDBC is not an JPA compliant ORM provider, it's a very lightweight implementation provided by Spring team.

#### YugabyteDB YSQL Dependency
```
<dependency>
    <groupId>com.yugabyte</groupId>
    <artifactId>spring-data-yugabytedb-ysql</artifactId>
    <version>2.3.0.RELEASE</version>
</dependency>
```

#### Design

- Implement a YugabyteDB YSQL dialect to support YSQL specific features
      a.  YB specific annotations for entity mapping and field mapping
             -  Primary key (Hash/Range)
             -  Compound keys
             -  Data types support
- Provide YugabytedbTemplate and  YugabytedbRepository implementation
     a. Transaction support
     b. Follower Reads support
     c. Index Support

- Implement `@enableyugabyteysqlrepositories` annotation

## Spring Data YCQL

YugabyteDB YCQL API is Cassandra CQL compliant API, YCQL is fully compatible with Spring Data Cassandra project with YugabyteDB YCQL Java driver. However, YugabyteDB has support for additional features which require enhancement to Spring Data projects. Here is the list of enhancements,

- Support for partition awareness, default YugabyteDB session object to use PartitionAwareLoadBalancing Policy
- Support for YugabyteDB ACID Transactions using Spring Transaction Manager
- Support for JSONB data type
- Support for micrometer, prometheus metrics to expose database client metrics
- Auto registering cassandra convertors, UDTs, complex type, JSONB

#### YugabyteDB YCQL Dependency
```
<dependency>
    <groupId>com.yugabyte</groupId>
    <artifactId>spring-data-yugabytedb-ycql</artifactId>
    <version>2.3.0.RELEASE</version>
</dependency>
```

### Design

1. Create a new Spring Data YCQL project to extend Spring Data cassandra (SDC) project. we can use SDC dependency and update the driver to use YCQL java driver

```
<dependency>
  <groupId>org.springframework.boot</groupId>
  <artifactId>spring-boot-starter-data-cassandra</artifactId>
  <exclusions>
    <exclusion>
	    <groupId>com.datastax.cassandra</groupId>
   	    <artifactId>cassandra-driver-core</artifactId>
    </exclusion>
</exclusions>
</dependency>
<dependency>
  <groupId>com.yugabyte</groupId>
  <artifactId>cassandra-driver-core</artifactId>
  <version>3.8.0-yb-5</version>
</dependency>
```

2. Externalize configurations properties

- Expose configurations properties as spring.data.yugabytedb.*
	
Example:

```
spring.data.yugabytedb.ycql.keyspace-name=cronos
spring.data.yugabytedb.ycql.contact-points=127.0.0.1
spring.data.yugabytedb.ycql.port=9042
```

- Cross compatibility between Cassandra and YugabyteDB properties
- Mock Testing support for YugabyteDB


3. Provide YugabytedbYCQLTemplate and  YugabytedbYCQLRepository implementation
     a. Transaction support
     b. Index support

4. Implement `@enableyugabyteycqlrepositories` and `@enabletransactionsupport `annotation
