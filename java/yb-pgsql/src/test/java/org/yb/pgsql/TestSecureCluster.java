// Copyright (c) YugabyteDB, Inc.
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
import org.yb.client.TestUtils;
import org.yb.YBTestRunner;

@RunWith(value=YBTestRunner.class)
public class TestSecureCluster extends BaseSecureClusterTest {
  public TestSecureCluster() throws Exception {
    super();
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
