// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThrows;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.alerts.impl.AlertChannelEmail;
import com.yugabyte.yw.common.alerts.impl.AlertChannelSlack;
import com.yugabyte.yw.models.AlertChannel.ChannelType;
import java.util.EnumSet;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class AlertChannelManagerTest extends FakeDBApplication {

  private static final EnumSet<ChannelType> IMPLEMENTED_TYPES =
      EnumSet.of(ChannelType.Email, ChannelType.Slack);

  @Mock private AlertChannelEmail emailChannel;

  @Mock private AlertChannelSlack slackChannel;

  @InjectMocks private AlertChannelManager channelManager;

  @Test
  public void testGet() {
    for (ChannelType channelType : ChannelType.values()) {
      if (IMPLEMENTED_TYPES.contains(channelType)) {
        assertNotNull(channelManager.get(channelType.name()));
      } else {
        assertThrows(IllegalArgumentException.class, () -> channelManager.get(channelType.name()));
      }
    }
  }
}
