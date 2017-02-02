package org.yb.loadtester.workload;

import org.apache.log4j.Logger;
import org.yb.loadtester.Workload;
import org.yb.loadtester.common.SimpleLoadGenerator.Key;

/**
 * This workload writes and reads some random string keys from a Redis server. One reader and one
 * writer thread thread each is spawned.
 */
public class RedisSimpleReadWrite extends Workload {
  private static final Logger LOG = Logger.getLogger(RedisSimpleReadWrite.class);
  // The number of keys to write.
  private static final int NUM_KEYS_TO_WRITE = 10;
  // The number of keys to read.
  private static final int NUM_KEYS_TO_READ = 10;
  // Static initialization of this workload's config.
  static {
    // Disable the read-write percentage.
    workloadConfig.readIOPSPercentage = -1;
    // Set the read and write threads to 1 each.
    workloadConfig.numReaderThreads = 1;
    workloadConfig.numWriterThreads = 1;
    // Set the number of keys to read and write.
    workloadConfig.numKeysToRead = NUM_KEYS_TO_READ;
    workloadConfig.numKeysToWrite = NUM_KEYS_TO_WRITE;
    workloadConfig.numUniqueKeysToWrite = NUM_KEYS_TO_WRITE;
  }

  @Override
  public void initialize(String args) {}

  @Override
  public void createTableIfNeeded() {
    LOG.info("Using the default .redis table, no need to create a table.");
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
}
