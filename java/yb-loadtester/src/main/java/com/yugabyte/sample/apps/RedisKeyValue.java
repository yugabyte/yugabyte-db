// Copyright (c) YugaByte, Inc.

package com.yugabyte.sample.apps;

import org.apache.log4j.Logger;

import com.yugabyte.sample.common.SimpleLoadGenerator.Key;

import java.util.Random;
import java.util.zip.Adler32;
import java.util.zip.Checksum;

/**
 * This workload writes and reads some random string keys from a Redis server. One reader and one
 * writer thread thread each is spawned.
 */
public class RedisKeyValue extends AppBase {
  private static final Logger LOG = Logger.getLogger(RedisKeyValue.class);
  // The number of unique keys to write.
  private static final int NUM_UNIQUE_KEYS = 1000000;
  // Static initialization of this workload's config.
  static {
    // Disable the read-write percentage.
    appConfig.readIOPSPercentage = -1;
    // Set the read and write threads to 1 each.
    appConfig.numReaderThreads = 32;
    appConfig.numWriterThreads = 2;
    // Set the number of keys to read and write.
    appConfig.numKeysToRead = -1;
    appConfig.numKeysToWrite = -1;
    appConfig.numUniqueKeysToWrite = NUM_UNIQUE_KEYS;
  }

  Random random = new Random();
  byte[] buffer;
  Checksum checksum = new Adler32();
  // For binary values we store checksum in bytes.
  static final int CHECKSUM_SIZE = 4;
  // For ASCII values we store checksum in hex string.
  static final int CHECKSUM_ASCII_SIZE = CHECKSUM_SIZE * 2;
  static final byte ASCII_MARKER = (byte) 'A';
  static final byte BINARY_MARKER = (byte) 'B';

  public RedisKeyValue() {
    buffer = new byte[appConfig.valueSize];
  }

  byte[] getRandomValue(Key key) {
    final int contentSize = appConfig.valueSize -
        (appConfig.restrictValuesToAscii ? CHECKSUM_ASCII_SIZE : CHECKSUM_SIZE);
    final byte[] keyValueBytes = key.getValueStr().getBytes();
    System.arraycopy(keyValueBytes, 0, buffer, 0,
        Math.min(contentSize, keyValueBytes.length));

    int i = keyValueBytes.length;
    if (appConfig.restrictValuesToAscii) {
      buffer[i++] = ASCII_MARKER;
      final int ASCII_START = 32;
      final int ASCII_RANGE_SIZE = 95;
      while (i < contentSize) {
        long r = (long) (random.nextInt() & 0xffffffff);
        // Hack to minimize number of calls to random.nextInt() in order to reduce CPU load.
        // This makes distribution non-uniform, but should be OK for load tests.
        for (int n = Math.min(Integer.BYTES, contentSize - i); n > 0;
             r /= ASCII_RANGE_SIZE, n--) {
          buffer[i++] = (byte) (ASCII_START + r % ASCII_RANGE_SIZE);
        }
      }
    } else {
      buffer[i++] = BINARY_MARKER;
      while (i < contentSize) {
        for (int r = random.nextInt(), n = Math.min(Integer.BYTES, contentSize - i); n > 0;
             r >>= Byte.SIZE, n--)
          buffer[i++] = (byte) r;
      }
    }

    checksum.reset();
    checksum.update(buffer, 0, contentSize);
    long cs = checksum.getValue();
    if (appConfig.restrictValuesToAscii) {
      String csHexStr = Long.toHexString(cs);
      // Prepend zeros
      while (i < appConfig.valueSize - csHexStr.length()) {
        buffer[i++] = (byte) '0';
      }
      System.arraycopy(csHexStr.getBytes(), 0, buffer, i, csHexStr.length());
    } else {
      while (i < appConfig.valueSize) {
        buffer[i++] = (byte) cs;
        cs >>= Byte.SIZE;
      }
    }

    return buffer;
  }

  void verifyRandomValue(Key key, byte[] value) {
    String keyValueStr = key.getValueStr();
    String prefix = new String(value, 0, keyValueStr.getBytes().length);
    // Check prefix.
    if (!prefix.equals(keyValueStr)) {
      LOG.fatal("Value mismatch for key: " + key.toString() +
          ", expected to start with: " + keyValueStr +
          ", got: " + prefix);
    }
    final boolean isAscii = value[keyValueStr.getBytes().length] == ASCII_MARKER;
    // Verify checksum.
    final int checksumSize = isAscii ? CHECKSUM_ASCII_SIZE : CHECKSUM_SIZE;
    checksum.reset();
    checksum.update(value, 0, value.length - checksumSize);
    long expectedCs;
    if (isAscii) {
      String csHexStr = new String(value, value.length - checksumSize, checksumSize);
      expectedCs = Long.parseUnsignedLong(csHexStr, 16);
    } else {
      expectedCs = 0;
      for (int i = value.length - 1; i >= value.length - checksumSize; --i) {
        expectedCs <<= Byte.SIZE;
        expectedCs |= (value[i] & 0xFF);
      }
    }
    if (checksum.getValue() != expectedCs) {
      LOG.fatal("Value mismatch for key: " + key.toString() +
          ", expected checksum: " + expectedCs +
          ", got: " + checksum.getValue());
    }
  }


  @Override
  public long doRead() {
    Key key = getSimpleLoadGenerator().getKeyToRead();
    if (key == null) {
      // There are no keys to read yet.
      return 0;
    }
    if (appConfig.valueSize == 0) {
      String value = getRedisClient().get(key.asString());
      key.verify(value);
    } else {
      byte[] value = getRedisClient().get(key.asString().getBytes());
      verifyRandomValue(key, value);
    }
    LOG.debug("Read key: " + key.toString());
    return 1;
  }

  @Override
  public long doWrite() {
    Key key = getSimpleLoadGenerator().getKeyToWrite();
    String retVal;
    if (appConfig.valueSize == 0) {
      String value = key.getValueStr();
      retVal = getRedisClient().set(key.asString(), value);
    } else {
      retVal = getRedisClient().set(key.asString().getBytes(), getRandomValue(key));
    }
    if (retVal == null) {
      getSimpleLoadGenerator().recordWriteFailure(key);
      return 0;
    }
    LOG.debug("Wrote key: " + key.toString() + ", return code: " + retVal);
    getSimpleLoadGenerator().recordWriteSuccess(key);
    return 1;
  }

  @Override
  public String getWorkloadDescription(String optsPrefix, String optsSuffix) {
    StringBuilder sb = new StringBuilder();
    sb.append(optsPrefix);
    sb.append("Sample key-value app built on Redis. The app writes out 1M unique string keys each");
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("with a string value. There are multiple readers and writers that update these keys");
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("and read them indefinitely. Note that the number of reads and writes to perform");
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("can be specified as a parameter.");
    sb.append(optsSuffix);
    return sb.toString();
  }

  @Override
  public String getExampleUsageOptions(String optsPrefix, String optsSuffix) {
    StringBuilder sb = new StringBuilder();
    sb.append(optsPrefix);
    sb.append("--num_unique_keys " + appConfig.numUniqueKeysToWrite);
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("--num_reads " + appConfig.numKeysToRead);
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("--num_writes " + appConfig.numKeysToWrite);
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("--num_threads_read " + appConfig.numReaderThreads);
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("--num_threads_write " + appConfig.numWriterThreads);
    sb.append(optsSuffix);
    return sb.toString();
  }
}
