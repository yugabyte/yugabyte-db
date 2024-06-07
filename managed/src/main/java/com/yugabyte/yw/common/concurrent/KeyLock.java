/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.concurrent;

import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class KeyLock<T> {
  private final ReentrantLock globalLock = new ReentrantLock();
  private final Map<T, LockEntry> metricKeyLocks = new HashMap<>();

  public void acquireLock(T key) {
    LockEntry lockEntry;
    log.trace("Acquiring lock for key {}", key);
    globalLock.lock();
    try {
      lockEntry =
          metricKeyLocks.computeIfAbsent(
              key,
              k -> {
                log.trace("Adding lock entry for key {}", key);
                return new LockEntry();
              });
      lockEntry.usages++;
    } finally {
      globalLock.unlock();
    }
    lockEntry.lock.lock();
    log.trace("Acquired lock for key {}", key);
  }

  public void releaseLock(T key) {
    log.trace("Releasing lock for key {}", key);
    globalLock.lock();
    try {
      LockEntry lockEntry = metricKeyLocks.get(key);
      lockEntry.usages--;
      if (lockEntry.usages == 0) {
        log.trace("Removing lock entry for key {}", key);
        metricKeyLocks.remove(key);
      }
      lockEntry.lock.unlock();
      log.trace("Released lock for key {}", key);
    } finally {
      globalLock.unlock();
    }
  }

  private static class LockEntry {
    private final Lock lock = new ReentrantLock();
    private int usages = 0;
  }
}
