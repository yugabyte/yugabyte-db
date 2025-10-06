// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import com.yugabyte.yw.common.PlatformServiceException;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class XClusterConfigTaskBaseTest {

  @Test
  public void validateOutInboundReplicationTablesSameTables() {
    Set<String> outboundSourceTableIds =
        new HashSet<>(Arrays.asList("00004103000030008000000000004000"));
    Set<String> inboundSourceTableIds =
        new HashSet<>(Arrays.asList("00004103000030008000000000004000"));
    try {
      XClusterConfigTaskBase.validateOutInboundReplicationTables(
          outboundSourceTableIds, inboundSourceTableIds);
    } catch (PlatformServiceException e) {
      fail();
    }
  }

  @Test
  public void validateOutInboundReplicationTablesOnlyInOutbound() {
    Set<String> outboundSourceTableIds =
        new HashSet<>(
            Arrays.asList("00004103000030008000000000004000", "00004104000030008000000000004004"));
    Set<String> inboundSourceTableIds =
        new HashSet<>(Arrays.asList("00004103000030008000000000004000"));
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                XClusterConfigTaskBase.validateOutInboundReplicationTables(
                    outboundSourceTableIds, inboundSourceTableIds));
    assertTrue(exception.getMessage().contains("are in outbound replication but not inbound"));
  }

  @Test
  public void validateOutInboundReplicationTablesOnlyInInbound() {
    Set<String> outboundSourceTableIds =
        new HashSet<>(Arrays.asList("00004103000030008000000000004000"));
    Set<String> inboundSourceTableIds =
        new HashSet<>(
            Arrays.asList("00004103000030008000000000004000", "00004104000030008000000000004004"));
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                XClusterConfigTaskBase.validateOutInboundReplicationTables(
                    outboundSourceTableIds, inboundSourceTableIds));
    assertTrue(exception.getMessage().contains("are in inbound replication but not outbound"));
  }
}
