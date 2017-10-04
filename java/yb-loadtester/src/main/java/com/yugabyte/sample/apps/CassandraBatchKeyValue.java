// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

package com.yugabyte.sample.apps;

import java.nio.ByteBuffer;
import java.util.HashSet;

import org.apache.log4j.Logger;

import com.datastax.driver.core.BatchStatement;
import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.ResultSet;
import com.yugabyte.sample.common.CmdLineOpts;
import com.yugabyte.sample.common.SimpleLoadGenerator.Key;

/**
 * This workload writes and reads some random string keys from a CQL server in batches. By default,
 * this app inserts a million keys, and reads/updates them indefinitely.
 */
public class CassandraBatchKeyValue extends CassandraKeyValue {
  private static final Logger LOG = Logger.getLogger(CassandraBatchKeyValue.class);

  // Static initialization of this workload's config.
  static {
    // The number of keys to write in each batch.
    appConfig.cassandraBatchSize = 10;
  }

  @Override
  public long doWrite() {
    BatchStatement batch = new BatchStatement();
    HashSet<Key> keys = new HashSet<Key>();
    PreparedStatement insert = getPreparedInsert();
    try {
      for (int i = 0; i < appConfig.cassandraBatchSize; i++) {
        Key key = getSimpleLoadGenerator().getKeyToWrite();
        ByteBuffer value = null;
        if (appConfig.valueSize == 0) {
          value = ByteBuffer.wrap(key.getValueStr().getBytes());
        } else {
          value = ByteBuffer.wrap(getRandomValue(key));
        }
        keys.add(key);
        batch.add(insert.bind(key.asString(), value));
      }
      // Do the write to Cassandra.
      ResultSet resultSet = getCassandraClient().execute(batch);
      LOG.debug("Wrote keys count: " + keys.size() + ", return code: " + resultSet.toString());
      for (Key key : keys) {
        getSimpleLoadGenerator().recordWriteSuccess(key);
      }
      return keys.size();
    } catch (Exception e) {
      for (Key key : keys) {
        getSimpleLoadGenerator().recordWriteFailure(key);
      }
      throw e;
    }
  }

  @Override
  public String getWorkloadDescription(String optsPrefix, String optsSuffix) {
    StringBuilder sb = new StringBuilder();
    sb.append(optsPrefix);
    sb.append("Sample batch key-value app built on Cassandra. The app writes out 1M unique string");
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("key in batches, each key with a string value. There are multiple readers and");
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("writers that update these keys and read them indefinitely. Note that the batch");
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("size and the number of reads and writes to perform can be specified as parameters.");
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
    sb.append("--value_size " + appConfig.valueSize);
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("--num_threads_read " + appConfig.numReaderThreads);
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("--num_threads_write " + appConfig.numWriterThreads);
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("--batch_size " + appConfig.cassandraBatchSize);
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("--table_ttl_seconds " + appConfig.tableTTLSeconds);
    sb.append(optsSuffix);
    return sb.toString();
  }
}
