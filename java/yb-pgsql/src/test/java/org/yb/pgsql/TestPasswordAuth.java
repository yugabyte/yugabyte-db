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
package org.yb.pgsql;

import static org.yb.AssertionWrappers.*;

import java.sql.Connection;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Map;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.apache.commons.lang3.StringUtils;

import org.hamcrest.CoreMatchers;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.Before;

import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.util.SystemUtil;
import org.yb.pgsql.BasePgSQLTest;
import org.yb.pgsql.ConnectionBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.YBParameterizedTestRunner;
import org.yb.YBTestRunner;
import com.yugabyte.util.PSQLException;


@RunWith(value = YBTestRunner.class)
public class TestPasswordAuth extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPasswordAuth.class);
  private static final String INCORRECT_PASSWORD_AUTH_MSG =
    "password authentication failed for user";

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);

    // The MD5 auth method in an hba file allows authentication through
    // both MD5 and SCRAM methods.
    builder.addCommonTServerFlag("ysql_hba_conf_csv",
    "\"host all md5_user all md5\","    +
    "\"host all scram_user all md5 \"," +
    "\"host all all all trust\"");
  }

  // Setup users with MD5 and SCRAM based password
  // auth before running the tests.
  @Before
  public void setupAuthUsers() {
    // Create new users with MD5 & SCRAM credentials
    try (Connection connection = getConnectionBuilder().connect();
    Statement statement = connection.createStatement()) {

      for(AuthType authType : AuthType.values()) {
        statement.execute("SET password_encryption='" + authType.type + "'");
        statement.execute("CREATE USER " + authType.username + " WITH PASSWORD '"
          + authType.password + "'");
      }
      LOG.info("Created  auth test users");

    } catch (Exception e) {
      LOG.error("", e);
      fail ("Failed to setup users");
    }
  }

  // Try logging in with the given auth type.
  // Basic unit for this test.
  private void tryLogin(AuthType authType, boolean expectFail) throws Exception {
    String authString = authType.toString() + " password authentication";
    ConnectionBuilder loginConnBldr = getConnectionBuilder()
      .withUser(authType.username)
      .withPassword(authType.getPassword(expectFail));

    //  login.
    try (Connection connection = loginConnBldr.connect()) {
      // No-op if expected success.
      if(expectFail)
        fail(authString + " succeeded with wrong password");
    } catch (PSQLException e) {
      // No-op if expected failure.
      if(!expectFail) {
        if (StringUtils.containsIgnoreCase(e.getMessage(),
            INCORRECT_PASSWORD_AUTH_MSG)) {
          fail(authString + " failed with correct password");
        } else {
          fail(authString + " unexpected error message:'%s'" + e.getMessage());
        }
      }
    }
  }

  @Test
  public void testPasswordAuthLogin() throws Exception {
    for(AuthType type : AuthType.values()) {
      tryLogin(type, false);
      tryLogin(type, true);
    }
  }

  private enum AuthType {
    MD5("md5", "md5_user", "password"),
    SCRAM("scram-sha-256", "scram_user", "password");

    AuthType(String type, String username, String password) {
      this.type = type;
      this.username = username;
      this.password = password;
      this.falsePassword = password + "123";
    }

    public final String type;
    public final String username;
    public final String password;
    public final String falsePassword;
    public String getPassword(boolean expectFail) {
      return (expectFail ? this.falsePassword : this.password);
    }
  }
}
