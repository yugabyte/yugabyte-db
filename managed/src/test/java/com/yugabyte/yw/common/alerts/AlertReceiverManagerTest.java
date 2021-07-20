// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThrows;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.alerts.impl.AlertReceiverEmail;
import com.yugabyte.yw.models.AlertReceiver.TargetType;
import java.util.EnumSet;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class AlertReceiverManagerTest extends FakeDBApplication {

  private static final EnumSet<TargetType> IMPLEMENTED_TYPES = EnumSet.of(TargetType.Email);

  @Mock private AlertReceiverEmail emailReceiver;

  @InjectMocks private AlertReceiverManager receiversManager;

  @Test
  public void testGet() {
    for (TargetType targetType : TargetType.values()) {
      if (IMPLEMENTED_TYPES.contains(targetType)) {
        assertNotNull(receiversManager.get(targetType.name()));
      } else {
        assertThrows(IllegalArgumentException.class, () -> receiversManager.get(targetType.name()));
      }
    }
  }
}
