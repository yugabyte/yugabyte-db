// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.alerts.impl.AlertChannelEmail;
import com.yugabyte.yw.common.alerts.impl.AlertChannelSlack;
import com.yugabyte.yw.models.AlertChannel.ChannelType;
import java.util.EnumMap;
import java.util.Optional;

@Singleton
public class AlertChannelManager {

  private EnumMap<ChannelType, AlertChannelInterface> channelsMap =
      new EnumMap<>(ChannelType.class);

  @Inject
  public AlertChannelManager(
      AlertChannelEmail alertChannelEmail, AlertChannelSlack alertChannelSlack) {
    channelsMap.put(ChannelType.Email, alertChannelEmail);
    channelsMap.put(ChannelType.Slack, alertChannelSlack);
    // TODO: Add other implementations here.
  }

  /**
   * Returns correct channel handler using the passed channel type name.
   *
   * @param typeName
   * @return
   * @throws IllegalArgumentException if we don't have a correct handler for the passed type of
   *     channels.
   */
  public AlertChannelInterface get(String typeName) {
    return Optional.ofNullable(channelsMap.get(ChannelType.valueOf(typeName)))
        .orElseThrow(IllegalArgumentException::new);
  }
}
