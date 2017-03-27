package org.yb.loadtester.workload;

import org.apache.log4j.Logger;
import org.yb.loadtester.Workload;
import org.yb.loadtester.common.SimpleLoadGenerator.Key;

/**
 * This workload writes and reads some random string keys from a Redis server. One reader and one
 * writer thread thread each is spawned.
 */
public class RedisKeyValue extends Workload {
  private static final Logger LOG = Logger.getLogger(RedisKeyValue.class);
  // The number of unique keys to write.
  private static final int NUM_UNIQUE_KEYS = 1000000;
  // Static initialization of this workload's config.
  static {
    // Disable the read-write percentage.
    workloadConfig.readIOPSPercentage = -1;
    // Set the read and write threads to 1 each.
    workloadConfig.numReaderThreads = 32;
    workloadConfig.numWriterThreads = 2;
    // Set the number of keys to read and write.
    workloadConfig.numKeysToRead = -1;
    workloadConfig.numKeysToWrite = -1;
    workloadConfig.numUniqueKeysToWrite = NUM_UNIQUE_KEYS;
  }

  @Override
  public long doRead() {
    Key key = getSimpleLoadGenerator().getKeyToRead();
    if (key == null) {
      // There are no keys to read yet.
      return 0;
    }
    String value = getRedisClient().get(key.asString());
    key.verify(value);
    LOG.debug("Read key: " + key.toString());
    return 1;
  }

  @Override
  public long doWrite() {
    Key key = getSimpleLoadGenerator().getKeyToWrite();
    String retVal = getRedisClient().set(key.asString(), key.getValueStr());
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
    sb.append("--num_unique_keys " + workloadConfig.numUniqueKeysToWrite);
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("--num_reads " + workloadConfig.numKeysToRead);
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("--num_writes " + workloadConfig.numKeysToWrite);
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("--num_threads_read " + workloadConfig.numReaderThreads);
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("--num_threads_write " + workloadConfig.numWriterThreads);
    sb.append(optsSuffix);
    return sb.toString();
  }
}
