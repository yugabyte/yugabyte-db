// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import com.yugabyte.yw.forms.AlertChannelTemplatesExt;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertChannel;
import com.yugabyte.yw.models.Customer;

public interface AlertChannelInterface {

  /**
   * Sends a notification according to passed parameters.
   *
   * @return An error if the action failed, or null otherwise.
   */
  void sendNotification(
      Customer customer,
      Alert alert,
      AlertChannel channel,
      AlertChannelTemplatesExt channelTemplates)
      throws PlatformNotificationException;
}
