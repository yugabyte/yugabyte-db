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

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertNull;

import com.google.common.net.HostAndPort;
import com.stumbleupon.async.Deferred;
import java.util.Arrays;
import java.util.List;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.CommonTypes.PeerRole;
import org.yb.YBTestRunner;

@RunWith(value = YBTestRunner.class)
public class TestGetLeaderMasterHostAndPort {

  private static final HostAndPort MASTER1 = HostAndPort.fromParts("10.0.0.1", 7100);
  private static final HostAndPort MASTER2 = HostAndPort.fromParts("10.0.0.2", 7100);
  private static final HostAndPort MASTER3 = HostAndPort.fromParts("10.0.0.3", 7100);
  private static final List<HostAndPort> MASTERS = Arrays.asList(MASTER1, MASTER2, MASTER3);

  private AsyncYBClient asyncClient;
  private YBClient client;

  @Before
  public void setUp() {
    asyncClient = mock(AsyncYBClient.class);
    when(asyncClient.getMasterAddresses()).thenReturn(MASTERS);
    when(asyncClient.getDefaultAdminOperationTimeoutMs()).thenReturn(10000L);
    client = new YBClient(asyncClient);
  }

  private static GetMasterRegistrationResponse registration(PeerRole role) {
    return new GetMasterRegistrationResponse(0L, "uuid", role, null, null);
  }

  @Test(timeout = 30000)
  public void testOneUnresolvableMasterStillFindsLeader() {
    TabletClient client2 = mock(TabletClient.class);
    TabletClient client3 = mock(TabletClient.class);
    when(asyncClient.newMasterClient(MASTER1)).thenReturn(null);
    when(asyncClient.newMasterClient(MASTER2)).thenReturn(client2);
    when(asyncClient.newMasterClient(MASTER3)).thenReturn(client3);
    when(asyncClient.getMasterRegistration(client2))
        .thenReturn(Deferred.fromResult(registration(PeerRole.FOLLOWER)));
    when(asyncClient.getMasterRegistration(client3))
        .thenReturn(Deferred.fromResult(registration(PeerRole.LEADER)));

    assertEquals(MASTER3, client.getLeaderMasterHostAndPort());
  }

  @Test(timeout = 30000)
  public void testAllUnresolvableMastersReturnsNull() {
    when(asyncClient.newMasterClient(any())).thenReturn(null);

    assertNull(client.getLeaderMasterHostAndPort());
    verify(asyncClient, never()).getMasterRegistration(any());
  }
}
