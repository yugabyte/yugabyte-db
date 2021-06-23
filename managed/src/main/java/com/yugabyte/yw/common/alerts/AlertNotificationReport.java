/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.alerts;

import lombok.ToString;

import java.util.HashMap;
import java.util.Map;
import java.util.UUID;

@ToString
public class AlertNotificationReport {
  int totalRaiseAttempt;
  int totalResolveAttempt;
  int failedRaise;
  int failedResolve;
  private final Map<UUID, Integer> failuresByReceiver = new HashMap<>();
  boolean raiseOrResolve;

  public boolean isEmpty() {
    return totalRaiseAttempt + totalResolveAttempt == 0;
  }

  public void raiseAttempt() {
    totalRaiseAttempt++;
    raiseOrResolve = true;
  }

  public void resolveAttempt() {
    totalResolveAttempt++;
    raiseOrResolve = false;
  }

  public void failAttempt() {
    if (raiseOrResolve) {
      failedRaise++;
    } else {
      failedResolve++;
    }
  }

  public void failReceiver(UUID receiverUuid) {
    failuresByReceiver.put(receiverUuid, failuresByReceiver(receiverUuid) + 1);
  }

  public int failuresByReceiver(UUID receiverUuid) {
    return failuresByReceiver.getOrDefault(receiverUuid, 0);
  }
}
