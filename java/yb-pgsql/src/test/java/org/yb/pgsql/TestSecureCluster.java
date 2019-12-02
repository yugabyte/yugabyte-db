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

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.client.TestUtils;

import java.nio.file.FileSystem;
import java.nio.file.FileSystems;
import java.sql.Statement;
import java.util.*;

// This test tests node to node encryption only.
// Postgres connections to t-server and master are encrypted as well.
// But postgres client connections are not encrypted.
// Some extra work required to adopt BasePgSQLTest for using encrypted connection.
// Encrypted client connections are tested in pg_wrapper-test test now.
@RunWith(value=YBTestRunner.class)
public class TestSecureCluster extends BasePgSQLTest {
  private String certsDir = null;

  public TestSecureCluster() {
    super();
    FileSystem fs = FileSystems.getDefault();
    certsDir = fs.getPath(TestUtils.getBinDir()).resolve(
        fs.getPath("../ent/test_certs")).toString();
    useIpWithCertificate = true;
    certFile = String.format("%s/%s", certsDir, "ca.crt");
  }

  @Test
  public void testConnection() throws Exception {
    try (Statement statement = connection.createStatement()) {
      createSimpleTable("test", "v");
    }
  }

  @Override
  protected Map<String, String> getMasterAndTServerFlags() {
    Map<String, String> flagMap = super.getMasterAndTServerFlags();
    flagMap.put("use_node_to_node_encryption", "true");
    flagMap.put("allow_insecure_connections", "false");
    flagMap.put("certs_dir", certsDir);
    return flagMap;
  }
}
