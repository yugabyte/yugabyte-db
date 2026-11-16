// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import java.time.Duration;
import java.util.AbstractQueue;
import java.util.Collection;
import java.util.Iterator;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.locks.Condition;
import java.util.concurrent.locks.ReentrantLock;

/**
 * Single-slot queue that coalesces submissions. The first offered element becomes available to a
 * consumer only after the configured delay. Elements offered while one is already pending are
 * dropped but still reported as accepted, so they are merged into the pending one.
 */
public class CoalescingDelayQueue<T> extends AbstractQueue<T> implements BlockingQueue<T> {
  private final ReentrantLock lock = new ReentrantLock();
  private final Condition changed = lock.newCondition();
  private final long delayNanos;

  private T element;
  private long readyAtNanos;

  public CoalescingDelayQueue(Duration delay) {
    this.delayNanos = delay.toNanos();
  }

  @Override
  public boolean offer(T e) {
    if (e == null) {
      throw new NullPointerException();
    }
    lock.lock();
    try {
      if (element == null) {
        element = e;
        readyAtNanos = System.nanoTime() + delayNanos;
        changed.signalAll();
      }
      // Report success even when dropped to coalesce.
      return true;
    } finally {
      lock.unlock();
    }
  }

  @Override
  public boolean offer(T e, long timeout, TimeUnit unit) {
    return offer(e);
  }

  @Override
  public void put(T e) {
    offer(e);
  }

  @Override
  public T take() throws InterruptedException {
    lock.lockInterruptibly();
    try {
      while (true) {
        if (element == null) {
          changed.await();
          continue;
        }
        long remainingNanos = readyAtNanos - System.nanoTime();
        if (remainingNanos <= 0) {
          return removeLocked();
        }
        changed.awaitNanos(remainingNanos);
      }
    } finally {
      lock.unlock();
    }
  }

  @Override
  public T poll(long timeout, TimeUnit unit) throws InterruptedException {
    long nanos = unit.toNanos(timeout);
    lock.lockInterruptibly();
    try {
      while (true) {
        if (element == null) {
          if (nanos <= 0) {
            return null;
          }
          nanos = changed.awaitNanos(nanos);
          continue;
        }
        long remainingNanos = readyAtNanos - System.nanoTime();
        if (remainingNanos <= 0) {
          return removeLocked();
        }
        if (nanos <= 0) {
          return null;
        }
        nanos = changed.awaitNanos(Math.min(nanos, remainingNanos));
      }
    } finally {
      lock.unlock();
    }
  }

  /** Returns the pending element only if its delay has elapsed. */
  @Override
  public T poll() {
    lock.lock();
    try {
      if (element == null || readyAtNanos - System.nanoTime() > 0) {
        return null;
      }
      return removeLocked();
    } finally {
      lock.unlock();
    }
  }

  /** Returns the pending element, whether or not its delay has elapsed. */
  @Override
  public T peek() {
    lock.lock();
    try {
      return element;
    } finally {
      lock.unlock();
    }
  }

  @Override
  public int size() {
    lock.lock();
    try {
      return element == null ? 0 : 1;
    } finally {
      lock.unlock();
    }
  }

  @Override
  public int remainingCapacity() {
    return 1 - size();
  }

  @Override
  public boolean remove(Object o) {
    if (o == null) {
      return false;
    }
    lock.lock();
    try {
      if (!o.equals(element)) {
        return false;
      }
      removeLocked();
      return true;
    } finally {
      lock.unlock();
    }
  }

  @Override
  public void clear() {
    lock.lock();
    try {
      if (element != null) {
        removeLocked();
      }
    } finally {
      lock.unlock();
    }
  }

  /** Drains the pending element regardless of its delay, e.g. for shutdownNow. */
  @Override
  public int drainTo(Collection<? super T> c) {
    return drainTo(c, Integer.MAX_VALUE);
  }

  @Override
  public int drainTo(Collection<? super T> c, int maxElements) {
    if (c == null) {
      throw new NullPointerException();
    }
    if (c == this) {
      throw new IllegalArgumentException();
    }
    if (maxElements <= 0) {
      return 0;
    }
    lock.lock();
    try {
      if (element == null) {
        return 0;
      }
      c.add(removeLocked());
      return 1;
    } finally {
      lock.unlock();
    }
  }

  @Override
  public Iterator<T> iterator() {
    throw new UnsupportedOperationException("iterator() not supported");
  }

  private T removeLocked() {
    T e = element;
    element = null;
    readyAtNanos = 0;
    changed.signalAll();
    return e;
  }
}
