package org.yb.loadtester.workload;

import org.apache.log4j.Logger;
import org.yb.loadtester.Workload;
import org.yb.loadtester.common.Configuration.Node;
import org.yb.loadtester.common.SimpleLoadGenerator;
import org.yb.loadtester.common.SimpleLoadGenerator.Key;

import redis.clients.jedis.Jedis;

public class RedisSimpleReadWrite extends Workload {
  private static final Logger LOG = Logger.getLogger(RedisSimpleReadWrite.class);
  // The number of keys to write.
  private static final int NUM_KEYS_TO_WRITE = 10;
  // The number of keys to read.
  private static final int NUM_KEYS_TO_READ = 10;
  // Static initialization of this workload's config.
  static {
    workloadConfig.description =
        "This workload writes and reads some random string keys from a Redis server. One reader " +
        "and one writer thread thread each is spawned.";
    // Disable the read-write percentage.
    workloadConfig.readIOPSPercentage = -1;
    // Set the read and write threads to 1 each.
    workloadConfig.numReaderThreads = 1;
    workloadConfig.numWriterThreads = 1;
    // Set the number of keys to read and write.
    workloadConfig.numKeysToRead = NUM_KEYS_TO_READ;
    workloadConfig.numKeysToWrite = NUM_KEYS_TO_WRITE;
  }
  // Instance of the load generator.
  private static SimpleLoadGenerator loadGenerator = new SimpleLoadGenerator(0, NUM_KEYS_TO_WRITE);
  // The Java redis client.
  private Jedis jedisClient;

  @Override
  public void initialize(String args) {}

  @Override
  public void createTableIfNeeded() {
    LOG.info("Using the default .redis table, no need to create a table.");
  }

  @Override
  public boolean doRead() {
    Key key = loadGenerator.getKeyToRead();
    if (key == null) {
      // There are no keys to read yet.
      return false;
    }
    String value = getClient().get(key.asString());
    key.verify(value);
    LOG.info("Read key: " + key.toString());
    return true;
  }

  @Override
  public boolean doWrite() {
    Key key = loadGenerator.getKeyToWrite();
    String retVal = getClient().set(key.asString(), key.getValueStr());
    LOG.info("Wrote key: " + key.toString() + ", return code: " + retVal);
    loadGenerator.recordWriteSuccess(key);
    return true;
  }

  @Override
  public void terminate() {
    if (jedisClient != null) {
      jedisClient.close();
    }
  }

  private Jedis getClient() {
    if (jedisClient == null) {
      Node node = getRandomNode();
      jedisClient = new Jedis(node.getHost(), node.getPort());
    }
    return jedisClient;
  }
}
