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
import java.sql.Statement;

/**
 * Removes all objects owned by the test user from the database of the passed connection.
 */
public class UserObjectCleaner implements ClusterCleaner {
  private static final Logger LOG = LoggerFactory.getLogger(UserObjectCleaner.class);

  @Override
  public void clean(Connection connection) throws Exception {
    LOG.info("Cleaning-up postgres test user objects");

    try (Statement statement = connection.createStatement()) {
      statement.execute("RESET SESSION AUTHORIZATION");
      statement.execute("DROP OWNED BY " + BasePgSQLTest.TEST_PG_USER + " CASCADE");
    }
  }
}
