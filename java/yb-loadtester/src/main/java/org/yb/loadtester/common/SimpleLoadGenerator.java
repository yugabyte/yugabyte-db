// Copyright (c) YugaByte, Inc.

package org.yb.loadtester.common;

import java.util.Random;
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
  // The max key that was successfully written.
  AtomicInteger maxWrittenKey;
  // The max key that has been generated and handed out so far.
  AtomicInteger maxGeneratedKey;
  // Random number generator.
  Random random = new Random();

  public SimpleLoadGenerator(int startKey, int endKey) {
    this.startKey = startKey;
    this.endKey = endKey;
    maxWrittenKey = new AtomicInteger(-1);
    maxGeneratedKey = new AtomicInteger(-1);
  }

  public void recordWriteSuccess(Key key) {
    // TODO: fix this for multhreaded usage.
    maxWrittenKey.set(key.getInteger());
  }

  public void recordWriteFailure(Key key) {
    // TODO: implement.
  }

  public Key getKeyToWrite() {
    if (maxGeneratedKey.get() == endKey - 1) {
      return null;
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
    return new Key(random.nextInt(maxKey));
  }
}
