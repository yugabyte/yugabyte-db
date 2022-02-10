// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common;

import com.google.common.collect.ForwardingMap;
import java.time.Duration;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

/** A map which can notify a caller when it is empty after it is sealed (write is blocked). */
public class DrainableMap<K, V> extends ForwardingMap<K, V> {
  private final Map<K, V> delegate = Collections.synchronizedMap(new HashMap<>());
  private final AtomicReference<CountDownLatch> latchRef = new AtomicReference<>();

  @Override
  protected Map<K, V> delegate() {
    return delegate;
  }

  /** Seal the map to block further put/putIfAbsent. */
  public synchronized void sealMap() {
    latchRef.compareAndSet(null, new CountDownLatch(delegate.isEmpty() ? 0 : 1));
  }

  /** Wait for the map to become empty once it is sealed. */
  public boolean waitForEmpty(Duration timeout) throws InterruptedException {
    CountDownLatch latch = latchRef.get();
    if (latch == null) {
      throw new IllegalStateException("Map is not sealed.");
    }
    return latch.await(timeout.toMillis(), TimeUnit.MILLISECONDS);
  }

  @Override
  public synchronized V put(K key, V value) {
    CountDownLatch latch = latchRef.get();
    if (latch != null) {
      throw new IllegalStateException("Map is already sealed.");
    }
    return delegate.put(key, value);
  }

  @Override
  public synchronized V putIfAbsent(K key, V value) {
    CountDownLatch latch = latchRef.get();
    if (latch != null) {
      throw new IllegalStateException("Map is already sealed.");
    }
    return delegate.putIfAbsent(key, value);
  }

  @Override
  public synchronized V remove(Object key) {
    V value = delegate.remove(key);
    if (delegate.isEmpty()) {
      CountDownLatch latch = latchRef.get();
      if (latch != null) {
        latch.countDown();
      }
    }
    return value;
  }

  @Override
  public synchronized boolean remove(Object key, Object value) {
    boolean removed = delegate.remove(key, value);
    if (delegate.isEmpty()) {
      CountDownLatch latch = latchRef.get();
      if (latch != null) {
        latch.countDown();
      }
    }
    return removed;
  }
}
