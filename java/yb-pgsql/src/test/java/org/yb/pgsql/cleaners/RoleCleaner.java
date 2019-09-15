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

package org.yb.pgsql.cleaners;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.pgsql.BasePgSQLTest;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.List;

/**
 * Removes all non-standard roles (except the test role), as well as their permissions and
 * owned objects.
 * NOTE: This will fail if any roles own objects in databases other than `postgres`,
 * so {@link DatabaseCleaner} must be run first.
 */
public class RoleCleaner implements ClusterCleaner {
  private static final Logger LOG = LoggerFactory.getLogger(RoleCleaner.class);

  @Override
  public void clean(Connection connection) throws Exception {
    LOG.info("Cleaning-up postgres roles and permissions");

    try (Statement statement = connection.createStatement()) {
      statement.execute("RESET SESSION AUTHORIZATION");

      ResultSet resultSet = statement.executeQuery(
          "SELECT rolname FROM pg_roles" +
              " WHERE rolname <> 'postgres'" +
              " AND rolname <> 'yugabyte'" +
              " AND rolname <> '" + BasePgSQLTest.TEST_PG_USER + "'" +
              " AND rolname NOT LIKE 'pg_%'");

      List<String> roles = new ArrayList<>();
      while (resultSet.next()) {
        roles.add(resultSet.getString(1));
      }

      for (String role : roles) {
        statement.execute("DROP OWNED BY " + role + " CASCADE");
        statement.execute("DROP ROLE " + role);
      }
    }
  }
}
