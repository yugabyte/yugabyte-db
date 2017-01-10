// Copyright (c) YugaByte, Inc.

package org.yb.loadtester.common;

import java.util.Random;
import java.util.Set;
import java.util.HashSet;
import java.util.concurrent.atomic.AtomicInteger;

import org.apache.log4j.Logger;


public class SimpleLoadGenerator {

  public static class Key {
    private static final Logger LOG = Logger.getLogger(Key.class);

    // The underlying key is an integer.
    Integer key;

    public Key(int key) {
      this.key = new Integer(key);
    }

    public int getInteger() {
      return key;
    }

    public String asString() {
      return key.toString();
    }

    public String getValueStr() {
      return ("val:" + key.toString());
    }

    public void verify(String value) {
      if (!value.equals(getValueStr())) {
        LOG.fatal("Value mismatch for key: " + key.toString() +
                  ", expected: " + getValueStr() +
                  ", got: " + value);
      }
    }

    @Override
    public String toString() {
      return "Key: " + key + ", value: " + getValueStr();
    }
  }

  // The key to start from.
  final int startKey;
  // The key to write till.
  final int endKey;
  // The max key that was successfully written consecutively.
  AtomicInteger maxWrittenKey;
  // The max key that has been generated and handed out so far.
  AtomicInteger maxGeneratedKey;
  // Set of keys that failed to write.
  Set<Integer> failedKeys;
  // Keys that have been written above maxWrittenKey.
  Set<Integer> writtenKeys;
  // A background thread to track keys written and increment maxWrittenKey.
  Thread writtenKeysTracker;
  // Random number generator.
  Random random = new Random();

  public SimpleLoadGenerator(int startKey, int endKey) {
    this.startKey = startKey;
    this.endKey = endKey;
    maxWrittenKey = new AtomicInteger(-1);
    maxGeneratedKey = new AtomicInteger(-1);
    failedKeys = new HashSet<Integer>();
    writtenKeys = new HashSet<Integer>();
    writtenKeysTracker = new Thread("Written Keys Tracker") {
        @Override
        public void run() {
          do {
            int key = maxWrittenKey.get() + 1;
            synchronized (this) {
              if (failedKeys.contains(key) || writtenKeys.remove(key)) {
                maxWrittenKey.set(key);
              } else {
                try {
                  wait();
                } catch (InterruptedException e) {
                  // Ignore
                }
              };
            }
          } while (true);
        }
      };
    // Make tracker a daemon thread so that it will not block the load tester from exiting
    // when done.
    writtenKeysTracker.setDaemon(true);
    writtenKeysTracker.start();
  }

  public void recordWriteSuccess(Key key) {
    synchronized (writtenKeysTracker) {
      writtenKeys.add(key.getInteger());
      writtenKeysTracker.notify();
    }
  }

  public void recordWriteFailure(Key key) {
    synchronized (writtenKeysTracker) {
      failedKeys.add(key.getInteger());
      writtenKeysTracker.notify();
    }
  }

  public Key getKeyToWrite() {
    if (maxGeneratedKey.get() == endKey - 1) {
      maxGeneratedKey.set(-1);
    }
    return new Key(maxGeneratedKey.incrementAndGet());
  }

  public Key getKeyToRead() {
    int maxKey = maxWrittenKey.get();
    if (maxKey < 0) {
      return null;
    } else if (maxKey == 0) {
      return new Key(0);
    }
    do {
      int key = random.nextInt(maxKey);
      if (!failedKeys.contains(key))
        return new Key(key);
    } while (true);
  }
}
