// Copyright (c) YugaByte, Inc.

package com.yugabyte.sample.apps;

import java.util.List;

import org.apache.log4j.Logger;

import com.datastax.driver.core.Cluster;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.Session;

/**
 * A very simple app that creates an Employees table, inserts an employee record and reads it back.
 */
public class CassandraHelloWorld extends AppBase {
  private static final Logger LOG = Logger.getLogger(CassandraHelloWorld.class);

  /**
   * Run this simple app.
   */
  @Override
  public void run() {
    try {
      // Create a Cassandra client.
      Cluster cluster = Cluster.builder().addContactPointsWithPorts(getNodesAsInet()).build();
      Session session = cluster.connect();

      // Create the table with a key column 'k' and a value column 'v'.
      String createTableStatement =
          "CREATE TABLE IF NOT EXISTS Employee (id int primary key, name varchar, age int);";
      ResultSet createResult = session.execute(createTableStatement);
      LOG.info("Created a table with CQL statement: " + createTableStatement);

      // Write a key-value pair.
      String insertStatement = "INSERT INTO Employee (id, name, age) VALUES (1, 'John', 35);";
      ResultSet insertResult = session.execute(insertStatement);
      LOG.info("Inserted a key-value pair with CQL statement: " + insertStatement);

      // Read the key-value pair back.
      String selectStatement = "SELECT name, age FROM Employee WHERE id = 1;";
      ResultSet selectResult = session.execute(selectStatement);
      List<Row> rows = selectResult.all();
      String name = rows.get(0).getString(0);
      int age = rows.get(0).getInt(1);
      // We expect only one row, with the key and value.
      LOG.info("Read data with CQL statement: " + selectStatement);
      LOG.info("Got result: row-count=" + rows.size() + ", name=" + name + ", age=" + age);

      // Close the client.
      session.close();
      cluster.close();
    } catch (Exception e) {
      LOG.error("Error running CassandraHelloWorld" + e.getMessage(), e);
    }
  }

  // Static initialization of this workload's config.
  static {
    // Set the app type to simple.
    appConfig.appType = AppConfig.Type.Simple;
  }

  @Override
  public String getWorkloadDescription(String optsPrefix, String optsSuffix) {
    StringBuilder sb = new StringBuilder();
    sb.append(optsPrefix);
    sb.append("A very simple hello world app built on Cassandra. The app writes one employee row");
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("into the 'Employee' table");
    sb.append(optsSuffix);
    return sb.toString();
  }
}
