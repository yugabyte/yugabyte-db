// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertReceiver;
import com.yugabyte.yw.models.Customer;

public interface AlertReceiverInterface {

  /**
   * Sends a notification according to passed parameters.
   *
   * @return An error if the action failed, or null otherwise.
   */
  void sendNotification(Customer customer, Alert alert, AlertReceiver receiver)
      throws YWNotificationException;
}
