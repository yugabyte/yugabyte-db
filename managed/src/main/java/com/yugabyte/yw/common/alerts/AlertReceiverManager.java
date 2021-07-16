// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.alerts.impl.AlertReceiverEmail;
import com.yugabyte.yw.models.AlertReceiver.TargetType;
import java.util.EnumMap;
import java.util.Optional;

@Singleton
public class AlertReceiverManager {

  private EnumMap<TargetType, AlertReceiverInterface> receiversMap =
      new EnumMap<>(TargetType.class);

  @Inject
  public AlertReceiverManager(AlertReceiverEmail alertReceiverEmail) {
    receiversMap.put(TargetType.Email, alertReceiverEmail);
    // TODO: Add other implementations here.
  }

  /**
   * Returns correct receiver handler using the passed receiver type name.
   *
   * @param typeName
   * @return
   * @throws IllegalArgumentException if we don't have a correct handler for the passed type of
   *     receivers.
   */
  public AlertReceiverInterface get(String typeName) {
    return Optional.ofNullable(receiversMap.get(TargetType.valueOf(typeName)))
        .orElseThrow(IllegalArgumentException::new);
  }
}
