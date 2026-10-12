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
package org.yb.client;

import static org.yb.AssertionWrappers.*;

import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.SocketAddress;
import java.util.concurrent.TimeUnit;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;

/**
 * Unit tests for TabletClient registration in {@code ip2client} / {@code client2tablets} and
 * removal via {@link AsyncYBClient#removeClientFromCache}.
 */
@RunWith(value = YBTestRunner.class)
public class TestTabletClientCache {

  private ServerSocket listener;
  private int port;
  private AsyncYBClient client;

  @Before
  public void setUp() throws Exception {
    listener = new ServerSocket();
    listener.bind(new InetSocketAddress("127.0.0.1", 0));
    port = listener.getLocalPort();
    // Dummy master address; these tests only exercise newClient against {@code listener}.
    client =
        new AsyncYBClient.AsyncYBClientBuilder("127.0.0.1:" + port)
            .defaultAdminOperationTimeoutMs(2000)
            .defaultSocketReadTimeoutMs(0)
            .build();
  }

  @After
  public void tearDown() throws Exception {
    if (client != null) {
      try {
        client.shutdown().join(2000);
      } catch (Exception ignored) {
        // Best-effort; caches under test may already be cleared.
      }
      client = null;
    }
    if (listener != null) {
      listener.close();
      listener = null;
    }
  }

  @Test
  public void testNewClientRegistersInBothCaches() throws Exception {
    TabletClient tc = client.newClient("test-uuid", "127.0.0.1", port);
    assertNotNull(tc);
    assertTrue(client.getTableClients().contains(tc));
    assertTrue(client.isClientInClient2Tablets(tc));
  }

  @Test
  public void testRemoveWithNullRemoteClearsBothCaches() throws Exception {
    TabletClient tc = client.newClient("test-uuid", "127.0.0.1", port);
    assertTrue(client.isClientInClient2Tablets(tc));

    client.removeClientFromCache(tc, null /* remote peer address */);

    assertFalse(client.getTableClients().contains(tc));
    assertFalse(client.isClientInClient2Tablets(tc));
  }

  @Test
  public void testRemoveWithMismatchedRemoteClearsBothCaches() throws Exception {
    TabletClient tc = client.newClient("test-uuid", "127.0.0.1", port);
    assertTrue(client.isClientInClient2Tablets(tc));

    // Host/port that does not match the ip2client key; identity fallback must still clear.
    SocketAddress mismatched = new InetSocketAddress("203.0.113.1", port + 1);
    client.removeClientFromCache(tc, mismatched);

    assertFalse(client.getTableClients().contains(tc));
    assertFalse(client.isClientInClient2Tablets(tc));
  }

  @Test
  public void testShutdownWithoutChannelClearsBothCaches() throws Exception {
    TabletClient tc = client.newClient("test-uuid", "127.0.0.1", port);
    assertTrue(client.isClientInClient2Tablets(tc));

    // Force the no-channel shutdown path used when connect never established a Channel.
    tc.doCleanup(null);
    tc.shutdown().join(2000);

    assertFalse(
        "dead client should be removed from ip2client", client.getTableClients().contains(tc));
    assertFalse(
        "dead client should be removed from client2tablets", client.isClientInClient2Tablets(tc));
  }

  @Test
  public void testConnectRefusedDoesNotLeaveStaleClient2TabletsEntry() throws Exception {
    listener.close();
    listener = null;

    TabletClient tc = client.newClient("test-uuid", "127.0.0.1", port);

    // Connection refused should mark the client dead and clear caches via close / cleanup paths.
    // If client2tablets.put ran after remove (the old race), a stale entry would remain here.
    long deadline = System.currentTimeMillis() + TimeUnit.SECONDS.toMillis(5);
    while (System.currentTimeMillis() < deadline) {
      if (!tc.isAlive() && !client.isClientInClient2Tablets(tc)) {
        break;
      }
      Thread.sleep(10);
    }

    assertFalse(tc.isAlive());
    assertFalse(client.getTableClients().contains(tc));
    assertFalse(client.isClientInClient2Tablets(tc));
  }
}
