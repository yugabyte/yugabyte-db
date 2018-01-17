// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

package com.yugabyte.sample.apps;

import java.util.Arrays;
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

      // Create the keyspace and use it.
      String createKeyspaceStatement =
          "CREATE KEYSPACE IF NOT EXISTS \"my_keyspace\";";
      ResultSet createKeyspaceResult = session.execute(createKeyspaceStatement);
      LOG.info("Created a keyspace with CQL statement: " + createKeyspaceStatement);
      String useKeyspaceStatement = "USE \"my_keyspace\";";
      ResultSet useKeyspaceResult = session.execute(useKeyspaceStatement);
      LOG.info("Use the new keyspace with CQL statement: " + useKeyspaceStatement);

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
  public List<String> getWorkloadDescription() {
    return Arrays.asList(
      "A very simple hello world app built on Cassandra. The app writes one employee row",
      "into the 'Employee' table");
  }
}
