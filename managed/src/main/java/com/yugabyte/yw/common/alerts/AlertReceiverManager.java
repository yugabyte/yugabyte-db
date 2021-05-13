// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import java.util.HashMap;
import java.util.Map;
import java.util.Optional;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.alerts.impl.AlertReceiverEmail;
import com.yugabyte.yw.models.AlertReceiver.TargetType;

@Singleton
public class AlertReceiverManager {

  private Map<TargetType, AlertReceiverInterface> receiversMap = new HashMap<>();

  @Inject
  public AlertReceiverManager(AlertReceiverEmail alertReceiverEmail) {
    receiversMap.put(TargetType.Email, alertReceiverEmail);
    // TODO: Add other implementations here.
  }

  /**
   * Returns correct receiver handler using the passed receiver type.
   *
   * @param targetType
   * @return
   * @throws IllegalArgumentException if we don't have a correct handler for the passed type of
   *     receivers.
   */
  public AlertReceiverInterface get(TargetType targetType) {
    return Optional.ofNullable(receiversMap.get(targetType))
        .orElseThrow(IllegalArgumentException::new);
  }
}
