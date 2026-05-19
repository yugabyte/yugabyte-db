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
package org.yb.util;

import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertTrue;

import com.google.common.net.HostAndPort;
import java.util.Arrays;
import java.util.Collections;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;

@RunWith(value = YBTestRunner.class)
public class TestPeerInfo {

  private static final int PORT = 7100;

  @Test
  public void testHasHostMatchesPrivateIp() {
    PeerInfo peer = new PeerInfo();
    peer.setLastKnownPrivateIps(
        Arrays.asList(HostAndPort.fromParts("10.0.0.1", PORT)));
    peer.setLastKnownBroadcastIps(Collections.emptyList());

    assertTrue(peer.hasHost("10.0.0.1"));
    assertFalse(peer.hasHost("10.0.0.2"));
  }

  @Test
  public void testHasHostMatchesBroadcastIp() {
    // K8s MCS case: peer's RPC address is the pod IP, but YBA stores the
    // server_broadcast_address as the node's private_ip. hasHost must match
    // against either list.
    PeerInfo peer = new PeerInfo();
    peer.setLastKnownPrivateIps(
        Arrays.asList(HostAndPort.fromParts("10.0.0.1", PORT)));
    peer.setLastKnownBroadcastIps(
        Arrays.asList(HostAndPort.fromParts("master-0.svc.cluster.local", PORT)));

    assertTrue(peer.hasHost("10.0.0.1"));
    assertTrue(peer.hasHost("master-0.svc.cluster.local"));
    assertFalse(peer.hasHost("10.0.0.2"));
    assertFalse(peer.hasHost("master-1.svc.cluster.local"));
  }

  @Test
  public void testHasHostNullSafe() {
    PeerInfo peer = new PeerInfo();
    peer.setLastKnownPrivateIps(
        Arrays.asList(HostAndPort.fromParts("10.0.0.1", PORT)));
    peer.setLastKnownBroadcastIps(
        Arrays.asList(HostAndPort.fromParts("master-0.svc.cluster.local", PORT)));

    assertFalse(peer.hasHost(null));
  }
}
