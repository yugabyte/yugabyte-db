package org.yb.loadtester.workload;

import java.util.List;

import org.apache.log4j.Logger;
import org.yb.loadtester.Workload;
import org.yb.loadtester.common.SimpleLoadGenerator;
import org.yb.loadtester.common.SimpleLoadGenerator.Key;

import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;

/**
 * This workload writes and reads some random string keys from a CQL server. One reader and one
 * writer thread thread each is spawned.
 */
public class CassandraSimpleReadWrite extends Workload {
  private static final Logger LOG = Logger.getLogger(CassandraSimpleReadWrite.class);
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
  }
  // Instance of the load generator.
  private static SimpleLoadGenerator loadGenerator = new SimpleLoadGenerator(0, NUM_KEYS_TO_WRITE);
  // The table name.
  private String tableName = CassandraSimpleReadWrite.class.getSimpleName();

  @Override
  public void initialize(String args) {}

  @Override
  public void createTableIfNeeded() {
    String create_stmt =
        String.format("CREATE TABLE %s (k varchar, v varchar, primary key (k));",
                      tableName);
    getCassandraClient().execute(create_stmt);
    LOG.info("Created a Cassandra table + " + tableName + " using query: [" + create_stmt + "]");
  }

  @Override
  public long doRead() {
    Key key = loadGenerator.getKeyToRead();
    if (key == null) {
      // There are no keys to read yet.
      return 0;
    }
    // Do the read from Cassandra.
    String select_stmt = String.format("SELECT k, v FROM %s WHERE k = '%s';",
                                       tableName, key.asString());
    ResultSet rs = getCassandraClient().execute(select_stmt);
    List<Row> rows = rs.all();
    if (rows.size() != 1) {
      LOG.fatal("Read [" + select_stmt + "], expected 1 row in result, got " + rows.size());
    }
    String value = rows.get(0).getString(1);
    key.verify(value);
    LOG.info("Read key: " + key.toString());
    return 1;
  }

  @Override
  public long doWrite() {
    Key key = loadGenerator.getKeyToWrite();
    // Do the write to Cassandra.
    String insert_stmt = String.format("INSERT INTO %s (k, v) VALUES ('%s', '%s');",
                                       tableName, key.asString(), key.getValueStr());
    ResultSet resultSet = getCassandraClient().execute(insert_stmt);
    LOG.info("Wrote key: " + key.toString() + ", return code: " + resultSet.toString());
    loadGenerator.recordWriteSuccess(key);
    return 1;
  }
}
