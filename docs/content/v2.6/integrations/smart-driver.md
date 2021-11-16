---
title: JDBC Smart Driver
linkTitle: JDBC Smart Driver
description: Smart JDBC driver for YSQL
menu:
  v2.6:
    identifier: smart-driver
    parent: integrations
    weight: 577
isTocNested: true
showAsideToc: true
---

JDBC Smart Driver for YSQL is a distributed driver based on the [PostgreSQL JDBC Driver](https://github.com/pgjdbc/pgjdbc).

The following are the key features of JDBC Smart Driver:

- It is **cluster-aware**, which eliminates the need for an external load balancer.

  The driver package includes a `YBClusterAwareDataSource` class that uses one initial contact point for the YugabyteDB cluster as a means of discovering all the nodes and, if required, refreshing the list of live endpoints with every new connection attempt. The refresh is triggered if stale information (older than 5 minutes) is discovered.

- It is **topology-aware**, wich is essential for geographically-distributed applications.

  The driver uses servers that are part of a set of geo-locations specified by topology keys.

## Creating the Smart Driver

You have a choice of obtaining the Smart Driver from Maven or creating it yourself.

### How to Obtain the Driver from Maven

To get the driver from Maven, add the following lines to your Maven project:

```xml
<dependency>
  <groupId>com.yugabyte</groupId>
  <artifactId>jdbc-yugabytedb</artifactId>
  <version>42.3.0</version>
</dependency>
```

### How to Build the Driver

To build the driver locally, follow this procedure:


To build the driver locally, follow this procedure:

0. Build environment

   gpgsuite needs to be present on the machine where build is performed.
   ```
   https://gpgtools.org/
   ```
   Please install gpg and create a key.

1. Clone the following repository:

   ```sh
   git clone https://github.com/yugabyte/pgjdbc.git && cd pgjdbc
   ```

2. Build and install into your local Maven folder by executing the following command:

   ```sh
   ./gradlew publishToMavenLocal -x test -x checkstyleMain
   ```

3. Add the following lines to your Maven project:

   ```xml
   <dependency>
       <groupId>com.yugabyte</groupId>
       <artifactId>jdbc-yugabytedb</artifactId>
       <version>42.3.0</version>
   </dependency> 
   ```


## Using the Smart Driver

**Cluster-aware** load balancing is performed using the `load-balance` connection property, which  takes `true` or `false` as valid values. In YBClusterAwareDataSource load balancing is true by default. However when using the DriverManager.getConnection() API the 'load-balance' property needs to be set to 'true'.

**Topology-aware** load balancing additionally requires the `topology-keys` connection property, which accepts a comma-separated list of geolocation values. You can specify a geolocation as cloud:region:zone.

To use the Smart Driver, do the following:

- Pass new connection properties for load balancing in the connection URL or properties bag.

  To enable uniform load balancing across all servers, you set the `load-balance` property to `true` in the URL, as per the following example:

  ```java
  String yburl = "jdbc:yugabytedb://127.0.0.1:5433/yugabyte?user=yugabyte&password=yugabyte&load-balance=true";
  DriverManager.getConnection(yburl);
  ```

  To specify topology keys, you set the `topology-keys` property to comma separated values, as per the following example:

  ```java
  String yburl = "jdbc:yugabytedb://127.0.0.1:5433/yugabyte?user=yugabyte&password=yugabyte&load-balance=true&topology-keys=cloud1:region1:zone1,cloud1:region1.zone2";
  DriverManager.getConnection(yburl);
  ```

- Configure `YBClusterAwareDataSource` for uniform load balancing and then use it to create a connection, as per the following example:

  ```java
  String jdbcUrl = "jdbc:yugabytedb://127.0.0.1:5433/yugabyte";
  YBClusterAwareDataSource ds = new YBClusterAwareDataSource();
  ds.setUrl(jdbcUrl);
  // Set topology keys to enable topology-aware distribution
  ds.setTopologyKeys("cloud1.region1.zone1,cloud1.region2.zone2");
  // Provide more end points to prevent first connection failure 
  // if an initial contact point is not available 
  ds.setAdditionalEndpoints("127.0.0.2:5433,127.0.0.3:5433");
  
  Connection conn = ds.getConnection();
  ```

- Configure `YBClusterAwareDataSource` with a pooling solution such as Hikari and then use it to create a connection, as per the following example:

  ```java
  Properties poolProperties = new Properties();
  poolProperties.setProperty("dataSourceClassName", "com.yugabyte.ysql.YBClusterAwareDataSource");
  poolProperties.setProperty("maximumPoolSize", 10);
  poolProperties.setProperty("dataSource.serverName", "127.0.0.1");
  poolProperties.setProperty("dataSource.portNumber", "5433");
  poolProperties.setProperty("dataSource.databaseName", "yugabyte");
  poolProperties.setProperty("dataSource.user", "yugabyte");
  poolProperties.setProperty("dataSource.password", "yugabyte");
  // Provide additional end points
  String additionalEndpoints = "127.0.0.2:5433,127.0.0.3:5433,127.0.0.4:5433,127.0.0.5:5433";
  poolProperties.setProperty("dataSource.additionalEndpoints", additionalEndpoints);
  // Load balance between specific geo-locations using topology keys
  String geoLocations = "cloud1.region1.zone1,cloud1.region2.zone2";
  poolProperties.setProperty("dataSource.topologyKeys", geoLocations);
  
  poolProperties.setProperty("poolName", name);
  
  HikariConfig config = new HikariConfig(poolProperties);
  config.validate();
  HikariDataSource ds = new HikariDataSource(config);
  
  Connection conn = ds.getConnection();
  ```

## Examples

To access a sample application that uses Yugabyte Smart Driver, visit [YugabyteDB JDBC driver](https://github.com/yugabyte/jdbc-yugabytedb).

To be able to use the samples, you need to complete the following steps: 

- Install YugabyteDB by following instructions provided in [Quick Start Guide](/latest/quick-start/install/). 

- Build the examples by running `mvn package`.

- Run the `run.sh` script, as per the following guideline:

  ```sh
  ./run.sh [-v] [-i] -D -<path_to_yugabyte_installation>
  ```

  In the preceding command, replace:

  - *[-v] [-i]* with `-v` if you want to run the script in `VERBOSE` mode.

  - *[-v] [-i]* with `-i` if you want to run the script in `INTERACTIVE` mode.

  - *[-v] [-i]* with `-v -i` if you want to run the script in both `VERBOSE` and `INTERACTIVE` mode at the same time.

  - *<path_to_yugabyte_installation>* with the path to the directory where you installed YugabyteDB. 

  \
  For more details, you can consult the help for the script.

  \

The following is an example of a shell command that runs the script:

```sh
./run.sh -v -i -D ~/yugabyte-2.7.2.0/
```

The `run` script starts a YugabyteDB cluster, demonstrates load balancing through Java applications, and then destroys the cluster.

In the beginning, the script displays a menu with two options: `UniformLoadBalance` and `TopologyAwareLoadBalance`. Selecting one of these options starts running the corresponding script with its Java application in the background.
