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

import java.util.Random;

/**
 * This workload writes and reads some random string keys from a Redis server. One reader and one
 * writer thread thread each is spawned.
 */
public class RedisKeyValue extends AppBase {
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

  public RedisKeyValue() {
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
