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

import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import java.sql.Connection;
import java.sql.SQLException;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.RequiresLinux;

@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestReservedStartupParameters extends BaseYsqlConnMgr {

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.replicationFactor(1);
  }

  private static final int SOCKET_TIMEOUT_MS = 10000;
  private static final String[][] RESERVED_PARAMETERS = {
    {"yb_ycm_internal_use_tserver_key_auth", "1"},
    {"yb_ycm_internal_is_client_ysqlconnmgr", "1"},
    {"yb_ycm_internal_authonly", "1"},
    {"yb_ycm_internal_is_control_conn", "1"},
    {"yb_ycm_internal_auth_remote_host", "192.0.2.1"},
    {"yb_ycm_internal_logical_conn_type", "E"},
    {"yb_ycm_internal_client_cert", "1"},
    {"yb_ycm_internal_future_parameter", "1"},
    {"yb_ycm_internal_client_addr", "192.168.1.1"},
    {"yb_ycm_internal_client_port", "9432"},
  };

  public TestReservedStartupParameters() {
    // Required for TLS connections to resolve correctly in the test environment;
    // certificates are issued for IP addresses, not hostnames.
    useIpWithCertificate = true;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("use_client_to_server_encryption", "true");
    flagMap.put("certs_for_client_dir", certsDir());
    return flagMap;
  }

  private static boolean errorContains(Throwable error, String expected) {
    for (Throwable cause = error; cause != null; cause = cause.getCause()) {
      if (cause.getMessage() != null && cause.getMessage().contains(expected)) {
        return true;
      }
    }
    return false;
  }

  /*
   * Below test validates startup parameters passed via "options" flag to database
   * are rejected.
   */
  @Test
  public void testRejectsConnectionManagerStartupParameters() throws Exception {
    for (String[] parameter : RESERVED_PARAMETERS) {
      try (Connection connection = getConnectionBuilder()
               .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
               .withOptions("-c " + parameter[0] + "=" + parameter[1])
               .connect()) {
        fail("Connection manager accepted reserved startup parameter " + parameter[0]);
      } catch (SQLException error) {
        assertTrue("Unexpected error for startup parameter " + parameter[0] + ": " + error,
            errorContains(error, "startup parameter \"" + parameter[0] + "\" is reserved"));
      }
    }
  }

  /*
   * Below test validates startup parameters passed directy as key value pair in connection string
   * gets rejected by connection manager.
   */
  @Test
  public void testRejectsRawConnectionManagerStartupParameters() throws Exception {
    for (String[] parameter : RESERVED_PARAMETERS) {
      assertReservedParameterRejected(parameter[0], parameter[1]);
    }
  }

  // Send a NON-SSL request to connection manager by explictly setting the startup
  // parameter for forwarding conn type in client startup message.
  @Test
  public void testCannotOverrideLogicalConnectionType() throws Exception {
    assertReservedParameterRejected("yb_ycm_internal_logical_conn_type", "E");
  }

  private void assertReservedParameterRejected(String name, String value) throws Exception {
    ErrorResponse error = rawConnBuilder()
        .socketTimeoutMs(SOCKET_TIMEOUT_MS)
        .startupParam(name, value)
        .connectExpectingError();
    assertTrue("Unexpected error for startup parameter " + name + ": " + error,
        error.toString().contains(
            "startup parameter \"" + name + "\" is reserved"));
  }
}
