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

import org.apache.log4j.Logger;

import com.yugabyte.sample.common.SimpleLoadGenerator.Key;

import java.util.Arrays;
import java.util.List;

/**
 * This workload writes and reads some random string keys from a Redis server. One reader and one
 * writer thread thread each is spawned.
 * Same as RedisKeyValue but uses the YBJedis client instead of Jedis.
 */
public class RedisYBClientKeyValue extends AppBase {
  private static final Logger LOG = Logger.getLogger(RedisKeyValue.class);

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
    appConfig.numUniqueKeysToWrite = AppBase.NUM_UNIQUE_KEYS;
  }

  public RedisYBClientKeyValue() {
    buffer = new byte[appConfig.valueSize];
  }

  @Override
  public long doRead() {
    Key key = getSimpleLoadGenerator().getKeyToRead();
    if (key == null) {
      // There are no keys to read yet.
      return 0;
    }
    if (appConfig.valueSize == 0) {
      String value = getYBJedisClient().get(key.asString());
      key.verify(value);
    } else {
      byte[] value = getYBJedisClient().get(key.asString().getBytes());
      verifyRandomValue(key, value);
    }
    LOG.debug("Read key: " + key.toString());
    return 1;
  }

  @Override
  public long doWrite(int threadIdx) {
    Key key = getSimpleLoadGenerator().getKeyToWrite();
    try {
      String retVal;
      if (appConfig.valueSize == 0) {
        String value = key.getValueStr();
        retVal = getYBJedisClient().set(key.asString(), value);
      } else {
        retVal = getYBJedisClient().set(key.asString().getBytes(), getRandomValue(key));
      }
      if (retVal == null) {
        getSimpleLoadGenerator().recordWriteFailure(key);
        return 0;
      }
      LOG.debug("Wrote key: " + key.toString() + ", return code: " + retVal);
      getSimpleLoadGenerator().recordWriteSuccess(key);
      return 1;
    } catch (Exception e) {
      getSimpleLoadGenerator().recordWriteFailure(key);
      throw e;
    }
  }

  @Override
  public List<String> getWorkloadDescription() {
    return Arrays.asList(
        "Sample key-value app built on Redis that uses the YBJedis (multi-node) client instead ",
        "of the default (one-node) Jedis one. The app writes out 1M unique string keys each",
        "with a string value. There are multiple readers and writers that update these keys",
        "and read them indefinitely. Note that the number of reads and writes to perform",
        "can be specified as a parameter.");
  }

  @Override
  public List<String> getExampleUsageOptions() {
    return Arrays.asList(
        "--num_unique_keys " + appConfig.numUniqueKeysToWrite,
        "--num_reads " + appConfig.numKeysToRead,
        "--num_writes " + appConfig.numKeysToWrite,
        "--num_threads_read " + appConfig.numReaderThreads,
        "--num_threads_write " + appConfig.numWriterThreads);
  }
}
