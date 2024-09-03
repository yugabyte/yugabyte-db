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

import org.hamcrest.CoreMatchers;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;

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
  }

  private static String getSearchPath (Statement stmt) {
      try (ResultSet rs = stmt.executeQuery("SHOW search_path")) {
          assertTrue("Expected one row while fetching search_path", rs.next());
          String returnString = rs.getString(1);
          return returnString;
      } catch (Exception e) {
          fail("Error while fetching search_path value");
      }
      return "";
  }

 @Test
 public void testEnquote() throws Exception {
    try (Connection connection = getConnectionBuilder()
                                    .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                                    .connect();
         Statement statement = connection.createStatement()) {

      // The value of search_path should remain same forever. With connection manager it was been
      // seen that with every transaction extra quotes been added around search_path, therefore
      // below iteration will ensure the value of search_path remains same through out.
      String expectedString = "\"$user\", public";
      for (int i = 0;i < 3;i++) {
        assertTrue("Unexpected value of search_path",
            getSearchPath(statement).equals(expectedString.replaceAll("\\\\", "")));
      }

      statement.execute("SET search_path TO oracle, \"$user\", public");
      expectedString = "oracle, \"$user\", public";
      for (int i = 0; i < 3; i++) {
        assertTrue("Unexpected value of search_path",
            getSearchPath(statement).equals(expectedString.replaceAll("\\\\", "")));
      }

      statement.execute("SET search_path TO 'some_path'");
      expectedString = "some_path";
      for (int i = 0; i < 3; i++) {
        assertTrue("Unexpected value of search_path",
            getSearchPath(statement).equals(expectedString.replaceAll("\\\\", "")));
      }

      statement.execute("SET search_path to 'oracle',\"$user\", public, 'some_path'");
      expectedString = "oracle, \"$user\", public, some_path";
      for (int i = 0; i < 3; i++) {
        assertTrue("Unexpected value of search_path",
            getSearchPath(statement).equals(expectedString.replaceAll("\\\\", "")));
      }

      statement.execute("SET search_path to \"$user\", '\"$some_value\"' ");
      expectedString = "\"$user\", \\\"\\\"\\\"$some_value\\\"\\\"\\\"";
      for (int i = 0; i < 3; i++) {
        assertTrue("Unexpected value of search_path",
            getSearchPath(statement).equals(expectedString.replaceAll("\\\\", "")));
      }

      statement.execute("SET search_path to '$user', '\"$some_value\"' ");
      expectedString = "\"$user\", \\\"\\\"\\\"$some_value\\\"\\\"\\\"";
      for (int i = 0; i < 3; i++) {
        assertTrue("Unexpected value of search_path",
            getSearchPath(statement).equals(expectedString.replaceAll("\\\\", "")));
      }

    }

 }

}
