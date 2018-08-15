---
title: IoT - Spark & Kafka
linkTitle: IoT Fleet Management
description: IoT Fleet Management - Spark and Kafka
aliases:
  - /develop/realworld-apps/iot-spark-kafka/
menu:
  1.1-beta:
    identifier: iot-spark-kafka
    parent: realworld-apps
    weight: 583
---

## Overview

This is an end-to-end functional application. It is a blueprint for an IoT application built on top of YugaByte DB (Cassandra API) as the database, Kafka as the message broker, Spark for realtime analytics and Spring Boot as the application framework. The stack used for this application is very similar to the SMACK stack (Spark, Mesos, Akka, YugaByte Cassandra, Kafka), which is a popular stack for developing IoT applications.


## Scenario

Assume that a fleet management company wants to track their fleet of vehicles which are delivering shipments. The vehicles performing the shipments are of different types (18 Wheelers, busses, large trucks, etc), and the shipments themselves happen over 3 routes (Route-37, Route-82, Route-43). The company wants to track:

- the breakdown of their vehicle types per shipment delivery route
- which vehicles are near road closures so that they can predict delays in deliveries

This app renders a dashboard showing both of the above. Below is a view of the realtime, auto-refreshing dashboard.

![YB IoT Fleet Management Dashboard](/images/develop/realworld-apps/iot-spark-kafka/yb-iot-fleet-management-screenshot.png)


## App Architecture

This application has the following subcomponents:

- Data Store - YugaByte
- Data Producer - Test program writing into Kafka
- Data Processor - Spark reading from Kafka
- Data Dashboard - Spring Boot app using web sockets, jQuery and bootstrap

We will look at each of these components in detail. Below is an architecture diagram showing how these components fit together.

![YB IoT Fleet Management Architecture](/images/develop/realworld-apps/iot-spark-kafka/yb-iot-fleet-mgmt-arch.png)


## Data Store
Stores all the user-facing data. YugaByte DB is used here, with CQL as the programming language.

All the data is stored in the keyspace `TrafficKeySpace`:
```
CREATE KEYSPACE IF NOT EXISTS TrafficKeySpace
```

There are three tables that hold the user-facing data - `Total_Traffic` for the lifetime traffic information, `Window_Traffic` for the last 30 seconds of traffic and `poi_traffic` for the traffic near a point of interest (road closures). The data processor constantly updates these tables, and the dashboard reads from these tables. Below are the schemas for these tables.

```
CREATE TABLE TrafficKeySpace.Total_Traffic (
    routeId text,
    vehicleType text,
    totalCount bigint,
    timeStamp timestamp,
    recordDate text,
    PRIMARY KEY (routeId, recordDate, vehicleType)
);

CREATE TABLE TrafficKeySpace.Window_Traffic (
    routeId text,
    vehicleType text,
    totalCount bigint,
    timeStamp timestamp,
    recordDate text,
    PRIMARY KEY (routeId, recordDate, vehicleType)
);

CREATE TABLE TrafficKeySpace.poi_traffic(
    vehicleid text,
    vehicletype text,
    distance bigint,
    timeStamp timestamp,
    PRIMARY KEY (vehicleid)
);
```


## Data Producer
A program that generates random test data and publishes it to the Kafka topic `iot-data-event`. This emulates the data received from the connected vehicles using a message broker in the real world.

A single data point is a JSON payload and looks as follows:
```
{
  "vehicleId":"0bf45cac-d1b8-4364-a906-980e1c2bdbcb",
  "vehicleType":"Taxi",
  "routeId":"Route-37",
  "longitude":"-95.255615",
  "latitude":"33.49808",
  "timestamp":"2017-10-16 12:31:03",
  "speed":49.0,
  "fuelLevel":38.0
}
```

## Data Processor

This is a Spark streaming application that consumes the data stream from the Kafka topic, converts them into meaningful insights and writes the resultant data back to YugaByte DB.

Spark communicates with YugaByte DB using the Cassandra connector. This is done as follows:
```
SparkConf conf =
  new SparkConf().setAppName(prop.getProperty("com.iot.app.spark.app.name"))
                 .set("spark.cassandra.connection.host",prop.getProperty("com.iot.app.cassandra.host"))
```

The data is consumed from a Kafka stream and collected in 5 second batches. This is achieved as follows:

```
JavaStreamingContext jssc = new JavaStreamingContext(conf,Durations.seconds(5));

JavaPairInputDStream<String, IoTData> directKafkaStream =
  KafkaUtils.createDirectStream(jssc,
                                String.class,
                                IoTData.class,
                                StringDecoder.class,
                                IoTDataDecoder.class,
                                kafkaParams,
                                topicsSet
                                );
```


It computes the following:

- Compute a breakdown by vehicle type and the shipment route across all the vehicles and shipments done so far
- Compute the above breakdown for active shipments. This is done by computing the breakdown by vehicle type and shipment route for the last 30 seconds
- Detect the vehicles which are within a 20 mile radius of a given Point of Interest (POI), which represents a road-closure



## Data Dashboard

This is a [Spring Boot](http://projects.spring.io/spring-boot/) application which queries the data from YugaByte and pushes the data to the webpage using [Web Sockets](http://docs.spring.io/spring/docs/current/spring-framework-reference/html/websocket.html#websocket-intro) and [jQuery](https://jquery.com/). The data is pushed to the web page in fixed intervals so data will be refreshed automatically. Dashboard displays data in charts and tables. This web page uses [bootstrap.js](http://getbootstrap.com/) to display the dashboard containing charts and tables.

We create entity classes for the three tables “Total_Traffic”, “Window_Traffic” and “Poi_Traffic”, and DAO interfaces for all the entities extending CassandraRepository. For example, we create the DAO class for TotalTrafficData entity as follows.

```
@Repository
public interface TotalTrafficDataRepository extends CassandraRepository<TotalTrafficData> {
  @Query("SELECT * FROM traffickeyspace.total_traffic WHERE recorddate = ? ALLOW FILTERING")
  Iterable<TotalTrafficData> findTrafficDataByDate(String date);   
}
```


In order to connect to YugaByte cluster and get connection for database operations, we write the assandraConfig class. This is done as follows:

```
public class CassandraConfig extends AbstractCassandraConfiguration {
  @Bean
  public CassandraClusterFactoryBean cluster() {
    // Create a Cassandra cluster to access YugaByte using CQL.
    CassandraClusterFactoryBean cluster = new CassandraClusterFactoryBean();
    // Set the database host.
    cluster.setContactPoints(environment.getProperty("com.iot.app.cassandra.host"));
    // Set the database port.
    cluster.setPort(Integer.parseInt(environment.getProperty("com.iot.app.cassandra.port")));
    return cluster;
  }
}
```


## Summary

This application is a blue print for building IoT applications. The instructions to build and run the application, as well as the source code can be found in [this github repo](https://github.com/YugaByte/yb-iot-fleet-management).
