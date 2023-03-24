// Copyright (c) Yugabyte, Inc.
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

import java.sql.Statement;
import java.util.Map;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;

import com.yugabyte.util.PSQLException;

@RunWith(value = YBTestRunner.class)
public class TestYbProfileFlag extends BasePgSQLTest {

  private static final Logger LOG = LoggerFactory.getLogger(TestPgSequences.class);
  private static final String USERNAME = "profile_user_1";
  private static final String PASSWORD = "profile_password";
  private static final String PROFILE_1_NAME = "prf1";

  @After
  public void cleanup() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("DROP USER %s", USERNAME));
    }
  }

  @Before
  public void setup() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("CREATE USER %s PASSWORD '%s'", USERNAME, PASSWORD));
    }
  }

  @Test(expected=PSQLException.class)
  public void testCreateProfileIsDisabled() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE PROFILE p1 FAILED_LOGIN_ATTEMPTS 3");
    }
  }

  @Test(expected=PSQLException.class)
  public void testDropProfileIsDisabled() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP PROFILE p1");
    }
  }

  @Test(expected=PSQLException.class)
  public void testAttachProfileIsDisabled() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("ALTER USER %s PROFILE %s", USERNAME, PROFILE_1_NAME));
    }
  }

  @Test(expected=PSQLException.class)
  public void testDetachProfileIsDisabled() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("ALTER USER %s NOPROFILE", USERNAME));
    }
  }

  @Test(expected=PSQLException.class)
  public void testUnlockUserIsDisabled() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("ALTER USER %s ACCOUNT UNLOCK", USERNAME));
    }
  }

  @Test(expected=PSQLException.class)
  public void testLockUserIsDisabled() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("ALTER USER %s ACCOUNT LOCK", USERNAME));
    }
  }
}
