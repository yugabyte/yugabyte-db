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


import org.apache.commons.io.FileUtils;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.client.TestUtils;
import org.yb.minicluster.MiniYBDaemon;
import org.yb.YBTestRunner;

import com.google.common.net.HostAndPort;

import java.io.File;
import java.nio.file.FileSystem;
import java.nio.file.FileSystems;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.Map;

import static org.yb.AssertionWrappers.assertEquals;

// This test tests node to node encryption only.
// Postgres connections to t-server and master are encrypted as well.
// But postgres client connections are not encrypted.
// Some extra work required to adopt BasePgSQLTest for using encrypted connection.
// Encrypted client connections are tested in pg_wrapper-test test now.
@RunWith(value=YBTestRunner.class)
public class TestSecureCluster extends BasePgSQLTest {
  private String srcCertsDir;
  private String certsDir;

  public TestSecureCluster() throws Exception {
    super();
    FileSystem fs = FileSystems.getDefault();
    srcCertsDir = fs.getPath(TestUtils.getBinDir()).resolve(
        fs.getPath("../test_certs")).toString();
    certsDir = fs.getPath(TestUtils.getBaseTmpDir()).toString();
    FileUtils.copyDirectory(new File(srcCertsDir), new File(certsDir));
    useIpWithCertificate = true;
    certFile = String.format("%s/%s", certsDir, "ca.crt");
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    flagMap.put("use_node_to_node_encryption", "true");
    flagMap.put("allow_insecure_connections", "false");
    flagMap.put("certs_dir", certsDir);
    return flagMap;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("use_node_to_node_encryption", "true");
    flagMap.put("allow_insecure_connections", "false");
    flagMap.put("certs_dir", certsDir);
    return flagMap;
  }

  @Test
  public void testConnection() throws Exception {
    createSimpleTable("test", "v");
    try (Statement stmt = connection.createStatement()) {
      stmt.executeUpdate("INSERT INTO test VALUES(1, 1, 1), (2, 2, 2)");
      try (ResultSet rs = stmt.executeQuery("SELECT * FROM test")) {
        assertEquals(2, getRowSet(rs).size());
      }
    }
  }

  @Test
  public void testCertificateReload() throws Exception {
    createSimpleTable("test", "v");

    FileUtils.copyDirectory(new File(String.format("%s/%s", srcCertsDir, "CA2")),
                            new File(certsDir));

    for (HostAndPort host : miniCluster.getTabletServers().keySet()) {
        runProcess(TestUtils.findBinary("yb-ts-cli"),
                   "--server_address",
                   host.toString(),
                   "--certs_dir_name",
                   srcCertsDir,
                   "reload_certificates");
    }
    connection.close();
    if (isTestRunningWithConnectionManager()) {
      // In this test single control connection is created.
      closeControlConnOnReloadConfig(1);
    }
    connection = getConnectionBuilder().connect();
    try (Statement stmt = connection.createStatement()) {
      stmt.executeUpdate("INSERT INTO test VALUES(1, 1, 1), (2, 2, 2)");
      try (ResultSet rs = stmt.executeQuery("SELECT * FROM test")) {
        assertEquals(2, getRowSet(rs).size());
      }
    }
  }

  @Test
  public void testYbAdmin() throws Exception {
    runProcess(TestUtils.findBinary("yb-admin"),
               "--master_addresses",
               masterAddresses,
               "--certs_dir_name",
               certsDir,
               "list_tables");
  }

  @Test
  public void testYbTsCli() throws Exception {
    runProcess(TestUtils.findBinary("yb-ts-cli"),
               "--server_address",
               miniCluster.getTabletServers().keySet().iterator().next().toString(),
               "--certs_dir_name",
               certsDir,
               "list_tablets");
  }
}
