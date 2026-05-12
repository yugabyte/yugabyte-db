// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations
// under the License.
//


package org.yb.ysqlconnmgr;

import static org.yb.AssertionWrappers.*;

import org.apache.commons.collections.functors.ExceptionClosure;
import org.hamcrest.CoreMatchers;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;

import java.lang.Thread.State;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.sql.Timestamp;
import java.util.Calendar;
import java.util.Date;
import java.util.Map;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.locks.Condition;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;

import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.fail;


import java.util.HashMap;
import java.util.Set;
import java.util.HashSet;
import java.util.Arrays;


import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;


@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestEnquote extends BaseYsqlConnMgr {

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    // Wanted to test the deploy phase quries runs without throwing an error.
    Map<String, String> additionalTserverFlags = new HashMap<String, String>() {
      {
        put("ysql_conn_mgr_optimized_session_parameters", "false");
      }
    };
    builder.addCommonTServerFlags(additionalTserverFlags);
  }

  private static String getGUCValue (Statement stmt, String guc) {
      try (ResultSet rs = stmt.executeQuery(String.format("SHOW %s", guc))) {
          assertTrue(
            String.format("Expected a row while fetching value of %s", guc), rs.next());
          String returnString = rs.getString(1);
          return returnString;
      } catch (Exception e) {
          fail("Error while fetching " + guc + " value " + e);
      }
      return "";
  }

 private void testEnquote(String guc_var, String default_value) throws Exception {
    try (Connection connection = getConnectionBuilder()
                                    .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                                    .connect();
         Statement statement = connection.createStatement()) {

      String expectedString;
      String actualString;

      for (int i = 0;i < 3;i++) {
        actualString = getGUCValue(statement, guc_var);
        assertTrue(
          String.format("Got a mismatch in the value of %s guc variable. " +
                        "actual: %s, expected: %s",
                        guc_var, actualString, default_value),
                        actualString.equals(default_value.replaceAll("\\\\", "")));
      }

      statement.execute(String.format("SET %s TO oracle, \"$user\", public", guc_var));
      expectedString = "oracle, \"$user\", public";
      for (int i = 0; i < 3; i++) {
        actualString = getGUCValue(statement, guc_var);
        assertTrue(
          String.format("Got a mismatch in the value of %s guc variable. " +
                        "actual: %s, expected: %s",
                        guc_var, actualString, expectedString),
                        actualString.equals(expectedString.replaceAll("\\\\", "")));
      }

      statement.execute(String.format("SET %s TO 'some_path', 'another_path'", guc_var));
      expectedString = "some_path, another_path";
      for (int i = 0; i < 3; i++) {
        actualString = getGUCValue(statement, guc_var);
        assertTrue(
          String.format("Got a mismatch in the value of %s guc variable. " +
                        "actual: %s, expected: %s",
                        guc_var, actualString, expectedString),
                        actualString.equals(expectedString.replaceAll("\\\\", "")));
      }

      statement.execute(String.format("SET %s TO 'oracle',\"$user\", public, 'some_path'",
                        guc_var));
      expectedString = "oracle, \"$user\", public, some_path";
      for (int i = 0; i < 3; i++) {
        actualString = getGUCValue(statement, guc_var);
        assertTrue(
          String.format("Got a mismatch in the value of %s guc variable. " +
                        "actual: %s, expected: %s",
                        guc_var, actualString, expectedString),
                        actualString.equals(expectedString.replaceAll("\\\\", "")));
      }

      statement.execute(String.format("SET %s TO \"$user\", '\"$some_value\"' ", guc_var));
      expectedString = "\"$user\", \\\"\\\"\\\"$some_value\\\"\\\"\\\"";
      for (int i = 0; i < 3; i++) {
        actualString = getGUCValue(statement, guc_var);
        assertTrue(
          String.format("Got a mismatch in the value of %s guc variable. " +
                        "actual: %s, expected: %s",
                        guc_var, actualString, expectedString),
                        actualString.equals(expectedString.replaceAll("\\\\", "")));
      }

      statement.execute(String.format("SET %s TO '$user', '\"$some_value\"' ", guc_var));
      expectedString = "\"$user\", \\\"\\\"\\\"$some_value\\\"\\\"\\\"";
      for (int i = 0; i < 3; i++) {
        actualString = getGUCValue(statement, guc_var);
        assertTrue(
          String.format("Got a mismatch in the value of %s guc variable. " +
                        "actual: %s, expected: %s",
                        guc_var, actualString, expectedString),
                        actualString.equals(expectedString.replaceAll("\\\\", "")));
      }

      // Test whitespaces

      statement.execute(String.format("SELECT pg_catalog.set_config('%s', '', false);", guc_var));
      // Connection manager will execute set guc_var to ''; in deploy phase
      // responsible for change in value on doing show.
      expectedString = "\"\"";
      for (int i = 0; i < 3; i++) {
        actualString = getGUCValue(statement, guc_var);
        assertTrue(String.format(
            "Got a mismatch in the value of %s guc variable. actual: %s, expected: %s", guc_var,
            actualString, expectedString), actualString.equals(expectedString));
      }

      statement.execute(String.format
        ("SELECT pg_catalog.set_config('%s', '       ', false);", guc_var));
      // Connection manager will execute set guc_var to ''; in deploy phase
      // responsible for change in value on doing show.
      expectedString = "\"\"";
      for (int i = 0; i < 3; i++) {
        actualString = getGUCValue(statement, guc_var);
        assertTrue(
          String.format("Got a mismatch in the value of %s guc variable. " +
                        "actual: %s, expected: %s",
                        guc_var, actualString, expectedString),
                        actualString.equals(expectedString));
      }

      statement.execute(String.format("SET %s TO '' ", guc_var));
      expectedString = "\"\"";
      for (int i = 0; i < 3; i++) {
        actualString = getGUCValue(statement, guc_var);
        assertTrue(
          String.format("Got a mismatch in the value of %s guc variable. " +
                        "actual: %s, expected: %s",
                        guc_var, actualString, expectedString),
                        actualString.equals(expectedString));
      }

      statement.execute(String.format("SET %s TO '  ' ", guc_var));
      expectedString = "\"  \"";
      for (int i = 0; i < 3; i++) {
        actualString = getGUCValue(statement, guc_var);
        assertTrue(
          String.format("Got a mismatch in the value of %s guc variable. " +
                        "actual: %s, expected: %s",
                        guc_var, actualString, expectedString),
                        actualString.equals(expectedString));
      }


    }
    catch (Exception e) {
      fail("Failure while processing " + guc_var + ": " + e.getMessage());
      LOG.error(String.format("Failure while processing %s", guc_var), e.getMessage());
    }

 }

 @Test
 public void testAvoidEnquoteGucVars() throws Exception {

  testEnquote("search_path", "\"$user\", public");
  testEnquote("session_preload_libraries", "");
  testEnquote("local_preload_libraries", "");
 }
}
