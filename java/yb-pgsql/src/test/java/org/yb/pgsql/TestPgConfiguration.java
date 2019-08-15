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

package org.yb.pgsql;

import org.hamcrest.CoreMatchers;
import org.junit.After;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.util.YBTestRunnerNonTsanOnly;

import java.sql.Connection;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.List;

import static org.yb.AssertionWrappers.assertThat;
import static org.yb.AssertionWrappers.fail;

/**
 * Tests for PostgreSQL configuration.
 */
@RunWith(value = YBTestRunnerNonTsanOnly.class)
public class TestPgConfiguration extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgConfiguration.class);

  @After
  public void cleanUpAfter() throws Exception {
    super.cleanUpAfter();
    // Restart cluster after each test.
    tearDownAfter();
  }

  @Test
  public void testPostgresConfigDefault() throws Exception {
    List<String> tserverArgs = new ArrayList<>(BasePgSQLTest.tserverArgs);
    int tserver = miniCluster.getNumTServers();
    miniCluster.startTServer(tserverArgs);

    try (Connection connection = newConnectionBuilder().setTServer(tserver).connect();
         Statement statement = connection.createStatement()) {
      // Default value determined by local initdb.
      assertQuery(
          statement,
          "SELECT setting, source FROM pg_settings WHERE name='max_connections'",
          new Row("100", "configuration file")
      );

      // Default value determined by the GUC.
      assertQuery(
          statement,
          "SELECT setting, source FROM pg_settings WHERE name='checkpoint_timeout'",
          new Row("300", "default")
      );
    }
  }

  @Test
  public void testPostgresConfigCatchAll() throws Exception {
    List<String> tserverArgs = new ArrayList<>(BasePgSQLTest.tserverArgs);
    tserverArgs.add("--ysql_pg_conf=max_connections=46, bonjour_name = 'some name', port=5432");
    int tserver = miniCluster.getNumTServers();
    miniCluster.startTServer(tserverArgs);

    try (Connection connection = newConnectionBuilder().setTServer(tserver).connect();
         Statement statement = connection.createStatement()) {
      // Parameters set via gflag.
      assertQuery(
          statement,
          "SELECT setting, source FROM pg_settings WHERE name='max_connections'",
          new Row("46", "configuration file")
      );
      assertQuery(
          statement,
          "SELECT setting, source FROM pg_settings WHERE name='bonjour_name'",
          new Row("some name", "configuration file")
      );

      // Port change is overridden by the tablet server.
      assertQuery(
          statement,
          "SELECT setting, source FROM pg_settings WHERE name='port'",
          new Row(String.valueOf(getPgPort(tserver)), "command line")
      );

      // Default value determined by local initdb.
      assertQuery(
          statement,
          "SELECT setting, source FROM pg_settings WHERE name='default_text_search_config'",
          new Row("pg_catalog.english", "configuration file")
      );
    }
  }

  @Test
  public void testPgHbaConfigDefault() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE ROLE test_role LOGIN PASSWORD 'pass'");
    }

    List<String> tserverArgs = new ArrayList<>(BasePgSQLTest.tserverArgs);
    int tserver = miniCluster.getNumTServers();
    miniCluster.startTServer(tserverArgs);

    // Can connect as test_role.
    try (Connection ignored = newConnectionBuilder().setTServer(tserver)
        .setUser("test_role").connect()) {
      // No-op.
    }

    // Can connect as superuser.
    try (Connection ignored = newConnectionBuilder().setTServer(tserver)
        .setUser("postgres").connect()) {
      // No-op.
    }
  }

  @Test
  public void testPgHbaConfigCatchAll() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE ROLE test_role LOGIN PASSWORD 'pass'");
    }

    List<String> tserverArgs = new ArrayList<>(BasePgSQLTest.tserverArgs);
    tserverArgs.add("--ysql_hba_conf=" +
        "host all test_role 0.0.0.0/0 password," +
        "host all all 0.0.0.0/0 trust");
    int tserver = miniCluster.getNumTServers();
    miniCluster.startTServer(tserverArgs);

    // Can connect as test_role with password.
    try (Connection ignored = newConnectionBuilder().setTServer(tserver)
        .setUser("test_role").setPassword("pass").connect()) {
      // No-op.
    }

    // Can connect as other users without password.
    try (Connection ignored = newConnectionBuilder().setTServer(tserver)
        .setUser("postgres").connect()) {
      // No-op.
    }

    // Cannot connect as test_role without password.
    try (Connection ignored = newConnectionBuilder().setTServer(tserver)
        .setUser("test_role").connect()) {
      fail("Expected login attempt to fail");
    } catch (SQLException sqle) {
      assertThat(
          sqle.getMessage(),
          CoreMatchers.containsString("no password was provided")
      );
    }
  }

  @Test
  public void testPgHbaConfigCatchAllReversed() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE ROLE test_role LOGIN PASSWORD 'pass'");
    }

    List<String> tserverArgs = new ArrayList<>(BasePgSQLTest.tserverArgs);
    tserverArgs.add("--ysql_hba_conf=" +
        "host all all 0.0.0.0/0 trust," +
        "host all test_role 0.0.0.0/0 password");
    int tserver = miniCluster.getNumTServers();
    miniCluster.startTServer(tserverArgs);

    // Can connect as test_role without password.
    try (Connection ignored = newConnectionBuilder().setTServer(tserver)
        .setUser("test_role").connect()) {
      // No-op.
    }

    // Can connect as superuser without password.
    try (Connection ignored = newConnectionBuilder().setTServer(tserver)
        .setUser("postgres").connect()) {
      // No-op.
    }
  }
}
