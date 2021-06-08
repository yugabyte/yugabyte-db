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
package org.yb.clientent;

import java.nio.file.FileSystem;
import java.nio.file.FileSystems;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.client.AsyncYBClient;
import org.yb.client.TestUtils;
import org.yb.client.TestYBClient;
import org.yb.client.YBClient;
import org.yb.util.Timeouts;
import static org.yb.AssertionWrappers.assertFalse;

@RunWith(value=YBTestRunner.class)
public class TestSSLClient extends TestYBClient {

  private void setup() throws Exception {
    destroyMiniCluster();

    certFile = String.format("%s/%s", certsDir(), "ca.crt");
    useIpWithCertificate = true;

    String certDirs = String.format("--certs_dir=%s", certsDir());

    List<String> flagsToAdd = Arrays.asList(
        "--use_node_to_node_encryption=true", "--use_client_to_server_encryption=true",
        "--allow_insecure_connections=false", certDirs);

    List<List<String>> tserverArgs = new ArrayList<List<String>>();
    tserverArgs.add(flagsToAdd);
    tserverArgs.add(flagsToAdd);
    tserverArgs.add(flagsToAdd);

    createMiniCluster(3, flagsToAdd, tserverArgs);
  }

  /**
   * Test to check that client connection succeeds when built with the correct cert.
   * @throws Exception
   */
  @Test(timeout = 100000)
  public void testClientCorrectCertificate() throws Exception {
    LOG.info("Starting testClientCorrectCertificate");

    setup();

    YBClient myClient = null;

    AsyncYBClient aClient = new AsyncYBClient.AsyncYBClientBuilder(masterAddresses)
                            .sslCertFile(certFile)
                            .build();
    myClient = new YBClient(aClient);
    myClient.waitForMasterLeader(Timeouts.adjustTimeoutSecForBuildType(10000));
    myClient.close();
    myClient = null;

  }

  /**
   * Test to check that client connection succeeds when provided a file with
   * multiple root certs.
   * @throws Exception
   */
  @Test(timeout = 100000)
  public void testClientMultiCertificate() throws Exception {
    LOG.info("Starting testClientMultiCertificate");

    setup();

    YBClient myClient = null;
    // The mutliCA cert has two different root certs, with the first entry in the file
    // being the pseudo root, which is not the one the server certs have been signed
    // with.
    String multiCA = String.format("%s/%s", certsDir(), "multiCA.crt");

    AsyncYBClient aClient = new AsyncYBClient.AsyncYBClientBuilder(masterAddresses)
                            .sslCertFile(multiCA)
                            .build();
    myClient = new YBClient(aClient);
    myClient.waitForMasterLeader(Timeouts.adjustTimeoutSecForBuildType(10000));
    myClient.close();
    myClient = null;
  }

  /**
   * Test to check that client connection fails when SSL is enabled but yb-client has SSL disabled.
   * @throws Exception
   */
  @Test(timeout = 100000)
  public void testClientNoCertificate() throws Exception {
    LOG.info("Starting testClientNoCertificate");

    boolean connectSuccess = true;
    setup();

    YBClient myClient = null;
    AsyncYBClient aClient = new AsyncYBClient.AsyncYBClientBuilder(masterAddresses)
                            .build();
    myClient = new YBClient(aClient);
    try {
      LOG.info("Trying to send an RPC...");
      myClient.waitForMasterLeader(Timeouts.adjustTimeoutSecForBuildType(10000));
    } catch (Exception e) {
      connectSuccess = false;
    }
    myClient.close();
    myClient = null;
    assertFalse(connectSuccess);

  }

  /**
   * Test to check that client connection fails when built with an incorrect cert.
   * @throws Exception
   */
  @Test(timeout = 100000)
  public void testClientIncorrectCertificate() throws Exception {
    LOG.info("Starting testClientIncorrectCertificate");

    boolean connectSuccess = true;
    setup();
    String incorrectCert = String.format("%s/%s", certsDir(), "pseudo.crt");

    YBClient myClient = null;
    AsyncYBClient aClient = new AsyncYBClient.AsyncYBClientBuilder(masterAddresses)
                            .sslCertFile(incorrectCert)
                            .build();
    myClient = new YBClient(aClient);
    try {
      LOG.info("Trying to send an RPC...");
      myClient.waitForMasterLeader(Timeouts.adjustTimeoutSecForBuildType(10000));
    } catch (Exception e) {
      connectSuccess = false;
    }
    myClient.close();
    myClient = null;
    assertFalse(connectSuccess);

  }

  private static String certsDir() {
    FileSystem fs = FileSystems.getDefault();
    return fs.getPath(TestUtils.getBinDir()).resolve(fs.getPath("../ent/test_certs")).toString();
  }
}
