package org.yb.loadtester.workload;

import org.apache.log4j.Logger;
import org.yb.loadtester.Workload;
import org.yb.loadtester.common.Configuration;
import org.yb.loadtester.common.Configuration.Node;
import org.yb.loadtester.common.SimpleLoadGenerator;
import org.yb.loadtester.common.SimpleLoadGenerator.Key;

import redis.clients.jedis.Jedis;

public class RedisSimpleReadWrite extends Workload {
  static {
    workloadConfig.description =
        "One reader and one writer thread, writes 10 string keys and reads 10 random keys";

    // Disable the read-write percentage.
    workloadConfig.readIOPSPercentage = -1;

    // Set the read and write threads to 1 each.
    workloadConfig.numReaderThreads = 1;
    workloadConfig.numWriterThreads = 1;
  }

  private static final Logger LOG = Logger.getLogger(RedisSimpleReadWrite.class);

  // The number of keys to write.
  private static final int NUM_KEYS_TO_WRITE = 10;
  // The number of keys to read.
  private static final int NUM_KEYS_TO_READ = 10;

  private Configuration configuration;
  private Jedis jedisClient;
  private static SimpleLoadGenerator loadGenerator;
  // The number of keys that have been read so far.
  private int numKeysRead = 0;
  // State variable to track if this workload has finished.
  private boolean hasFinished = false;

  @Override
  public void initialize(Configuration configuration, String args) {
    this.configuration = configuration;
    loadGenerator = new SimpleLoadGenerator(0, NUM_KEYS_TO_WRITE);
  }

  @Override
  public void createTableIfNeeded() {
    LOG.info("Using the default .redis table, no need to create a table.");
  }

  @Override
  public void doRead() {
    if (numKeysRead == NUM_KEYS_TO_READ - 1) {
      hasFinished = true;
      return;
    }
    Key key = loadGenerator.getKeyToRead();
    if (key == null) {
      return;
    }
    String value = getClient().get(key.asString());
    key.verify(value);
    LOG.info("Read key: " + key.toString());
    numKeysRead++;
  }

  @Override
  public void doWrite() {
    Key key = loadGenerator.getKeyToWrite();
    if (key == null) {
      hasFinished = true;
      return;
    }
    String retVal = getClient().set(key.asString(), key.getValueStr());
    LOG.info("Wrote key: " + key.toString() + ", return code: " + retVal);
    loadGenerator.recordWriteSuccess(key);
  }

  @Override
  public boolean hasFinished() {
    return hasFinished;
  }

  private Jedis getClient() {
    if (jedisClient == null) {
      Node node = configuration.getRandomNode();
      jedisClient = new Jedis(node.getHost(), node.getPort());
    }
    return jedisClient;
  }
}
