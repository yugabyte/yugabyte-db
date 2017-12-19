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

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.concurrent.Callable;

import org.apache.commons.math3.distribution.AbstractIntegerDistribution;
import org.apache.commons.math3.distribution.ZipfDistribution;
import org.apache.log4j.Logger;

import com.yugabyte.sample.common.RedisHashLoadGenerator.KeySubKey;
import com.yugabyte.sample.common.SimpleLoadGenerator.Key;

import redis.clients.jedis.Response;

/**
 * This workload uses HMGet/HMSet to read/write to the cluster.
 *
 * It allows users to specify the number of subkeys in each key, and also lets the user
 * choose how many subkeys should be read/updated in each operation.
 *
 * For each operation, the key to be operated upon is chosen uniformly at random. The subkeys
 * to be operated upon are chosen either uniformly at random, or if desired chosen using a
 * zifpian distribution.
 *
 * Similarly, the value sizes for each sub-key can be chosen based on a zifpian distribution.
 */
public class RedisHashPipelined extends RedisPipelinedKeyValue {
  private static final Logger LOG = Logger.getLogger(RedisHashPipelined.class);
  private int[] subkeyValueSize;
  private byte[][] subkeyValueBuffers;

  // Returns n such that 2 ^ (n-1) < val <= 2^n.
  private static int log2ceil(int val) {
    assert(val > 0);
    int n = 0;
    while (val > 0) {
      val = val >> 1;
      n++;
    }
    return n;
  }

  public RedisHashPipelined() {
    int kMinValueSize = 10; // Give enough room for the checksum.
    int kMaxValueSize = appConfig.maxValueSize;
    if (appConfig.valueSizeZipfExponent > 0) {
      int minBits = log2ceil(kMinValueSize);
      int maxBits = log2ceil(kMaxValueSize);

      AbstractIntegerDistribution valueSizeDist = new ZipfDistribution(
          maxBits - minBits + 1, appConfig.valueSizeZipfExponent);
      // Get (1 + numSubKey) value-sizes from the above distribution.
      // Scale up/down the values such that the expected-mean value is
      // appConfig.valueSize
      // Adjust values to make sure they are within [kMinValueSize,
      // kMaxValueSize]
      subkeyValueSize = valueSizeDist.sample(appConfig.numSubkeysPerKey + 1);
      Arrays.sort(subkeyValueSize);
      // Estimate the expected size of the subkey value size.
      AbstractIntegerDistribution freqDist =
          getRedisHashLoadGenerator().getSubkeyDistribution();
      double expected_size = 0;
      for (int i = 0; i < subkeyValueSize.length; i++) {
        subkeyValueSize[i] = (1 << (subkeyValueSize[i] + minBits - 1));
        expected_size += freqDist.probability(i) * subkeyValueSize[i];
      }
      LOG.debug("Expected size for the distribution is " +
                valueSizeDist.getNumericalMean());
      // Update the sizes so that the expected is appConfig.valueSize.
      for (int i = 0; i < subkeyValueSize.length; i++) {
        subkeyValueSize[i] = (int)Math.round(
            subkeyValueSize[i] * appConfig.valueSize / expected_size);
        // Set the min value size to be at least kMinValueSize.
        if (subkeyValueSize[i] < kMinValueSize) {
          LOG.debug("Updating value size for subkey[ " + i + "] from " +
                    subkeyValueSize[i] + " to " + kMinValueSize);
          subkeyValueSize[i] = kMinValueSize;
        }
        if (subkeyValueSize[i] > kMaxValueSize) {
          LOG.debug("Updating value size for subkey[ " + i + "] from " +
                    subkeyValueSize[i] + " to " + kMaxValueSize);
          subkeyValueSize[i] = kMaxValueSize;
        }
        LOG.info("Value size for subkey[ " + i + "] is " + subkeyValueSize[i]);
      }
    } else {
      subkeyValueSize = new int[appConfig.numSubkeysPerKey + 1];
      Arrays.fill(subkeyValueSize, appConfig.valueSize);
    }

    subkeyValueBuffers = new byte[subkeyValueSize.length][];
    for (int i = 0; i < subkeyValueSize.length; i++) {
      subkeyValueBuffers[i] = new byte[subkeyValueSize[i]];
    }
  }

  @Override
  public long doRead() {
    List<KeySubKey> keySubKeys =
        getRedisHashLoadGenerator().getKeySubkeysToRead();
    if (keySubKeys == null) {
      // There are no keys to read yet.
      return 0;
    }
    int numSubKeysToRead = keySubKeys.size();
    Key key = keySubKeys.get(0).getKey();
    if (appConfig.valueSize == 0) {
      String[] fields = new String[numSubKeysToRead];
      for (int i = 0; i < numSubKeysToRead; i++) {
        fields[i] = keySubKeys.get(i).getSubkey().asString();
      }
      Response<List<String>> resp =
          getRedisPipeline().hmget(key.asString(), fields);
      verifyReadString(keySubKeys, resp);
    } else {
      byte[][] fields = new byte[numSubKeysToRead][];
      for (int i = 0; i < numSubKeysToRead; i++) {
        fields[i] = keySubKeys.get(i).getSubkey().asString().getBytes();
      }
      Response<List<byte[]>> resp =
          getRedisPipeline().hmget(key.asString().getBytes(), fields);
      verifyReadBytes(keySubKeys, resp);
    }
    return flushPipelineIfNecessary();
  }

  private void verifyReadString(final List<KeySubKey> keySubKeys,
                                final Response<List<String>> resp) {
    pipelinedOpResponseCallables.add(new Callable<Integer>() {
      @Override
      public Integer call() throws Exception {
        List<String> received = resp.get();
        if (received.size() != keySubKeys.size()) {
          LOG.debug("Mismatch Received " + received.size() +
                    " responses for HMGET"
                    + " was expecting " + keySubKeys.size());
          return 0;
        }
        Iterator<KeySubKey> exptdIter = keySubKeys.iterator();
        Iterator<String> rcvdIter = received.iterator();
        while (rcvdIter.hasNext()) {
          KeySubKey exptd = exptdIter.next();
          String rcvd = rcvdIter.next();
          exptd.verify(rcvd);
        }
        return 1;
      }
    });
  }

  private void verifyReadBytes(final List<KeySubKey> keySubKeys,
                               final Response<List<byte[]>> resp) {
    pipelinedOpResponseCallables.add(new Callable<Integer>() {
      @Override
      public Integer call() throws Exception {
        List<byte[]> received = resp.get();
        if (received.size() != keySubKeys.size()) {
          LOG.error("Mismatch Received " + received.size() +
                    " responses for HMGET"
                    + " was expecting " + keySubKeys.size());
          return 0;
        }
        Iterator<KeySubKey> exptdIter = keySubKeys.iterator();
        Iterator<byte[]> rcvdIter = received.iterator();
        while (rcvdIter.hasNext()) {
          KeySubKey exptd = exptdIter.next();
          byte[] rcvd = rcvdIter.next();
          if (rcvd == null || !verifyRandomValue(exptd.getSubkey(), rcvd)) {
            LOG.error("Error in HMGet. for " + exptd.toString() + " got : " + rcvd);
            return 0;
          }
        }

        return 1;
      }
    });
  }

  @Override
  public long doWrite() {
    ArrayList<KeySubKey> keySubKeys =
        getRedisHashLoadGenerator().getKeySubkeysToWrite();
    Key key = keySubKeys.get(0).getKey();
    Response<String> resp;
    Response<Long> respHSet;
    if (appConfig.valueSize == 0) {
      HashMap<String, String> hash = new HashMap<>();
      for (KeySubKey ks : keySubKeys) {
        hash.put(ks.getSubkey().asString(), ks.getValueStr());
      }
      if (hash.size() == 1) { // Hack to avoid ENG-2644.
        KeySubKey ks = keySubKeys.get(0);
        respHSet = getRedisPipeline().hset(ks.getKey().asString(),
                                           ks.getSubkey().asString(), ks.getValueStr());
        verifyWriteResultInt(keySubKeys, respHSet);
      } else {
        resp = getRedisPipeline().hmset(key.asString(), hash);
        verifyWriteResult(keySubKeys, resp);
      }
    } else {
      HashMap<byte[], byte[]> hash = new HashMap<>();
      HashSet<String> uniqueSubkeys = new HashSet<>();
      for (KeySubKey ks : keySubKeys) {
        int size = subkeyValueSize[(int)ks.subkeyAsNumber()];
        LOG.debug("Writing to key-subkey " + ks.toString() + " with size " +
                  size);
        hash.put(ks.getSubkey().asString().getBytes(),
                 getRandomValue(ks.getSubkey(),
                                subkeyValueBuffers[(int)ks.subkeyAsNumber()]));
        uniqueSubkeys.add(ks.getSubkey().asString());
      }
      if (uniqueSubkeys.size() == 1) { // Hack to avoid ENG-2644.
        KeySubKey ks = keySubKeys.get(0);
        LOG.debug("Writing HSet for " + ks.toString() + " with size " +
                  subkeyValueSize[(int)ks.subkeyAsNumber()]);
        respHSet = getRedisPipeline().hset(
            key.asString().getBytes(), ks.getSubkey().asString().getBytes(),
            getRandomValue(ks.getSubkey(),
                           subkeyValueBuffers[(int)ks.subkeyAsNumber()]));
        verifyWriteResultInt(keySubKeys, respHSet);
      } else {
        resp = getRedisPipeline().hmset(key.asString().getBytes(), hash);
        verifyWriteResult(keySubKeys, resp);
      }
    }
    return flushPipelineIfNecessary();
  }

  private long verifyWriteResult(final ArrayList<KeySubKey> ks,
                                 final Response<String> retVal) {
    pipelinedOpResponseCallables.add(new Callable<Integer>() {
      @Override
      public Integer call() throws Exception {
        if (retVal.get() == null) {
          getRedisHashLoadGenerator().recordWriteFailure(ks);
          return 0;
        }
        getRedisHashLoadGenerator().recordWriteSuccess(ks);
        return 1;
      }
    });
    return 1;
  }

  private long verifyWriteResultInt(final ArrayList<KeySubKey> ks,
                                    final Response<Long> retVal) {
    pipelinedOpResponseCallables.add(new Callable<Integer>() {
      @Override
      public Integer call() throws Exception {
        if (retVal.get() == null) {
          getRedisHashLoadGenerator().recordWriteFailure(ks);
          return 0;
        }
        getRedisHashLoadGenerator().recordWriteSuccess(ks);
        return 1;
      }
    });
    return 1;
  }

  @Override
  public String getWorkloadDescription(String optsPrefix, String optsSuffix) {
    StringBuilder sb = new StringBuilder();
    sb.append(optsPrefix);
    sb.append("Sample redis hash-map based app built on RedisPipelined.");
    sb.append(optsSuffix);
    return sb.toString();
  }

  @Override
  public String getExampleUsageOptions(String optsPrefix, String optsSuffix) {
    StringBuilder sb = new StringBuilder();
    sb.append(super.getExampleUsageOptions(optsPrefix, optsSuffix));
    sb.append(optsPrefix);
    sb.append("--num_subkeys_per_key " + appConfig.numSubkeysPerKey);
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("--num_subkeys_per_write " + appConfig.numSubkeysPerWrite);
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("--num_subkeys_per_read " + appConfig.numSubkeysPerRead);
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("--key_freq_zipf_exponent " + appConfig.keyUpdateFreqZipfExponent);
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("--subkey_freq_zipf_exponent " + appConfig.subkeyUpdateFreqZipfExponent);
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("--subkey_value_size_zipf_exponent " + appConfig.valueSizeZipfExponent);
    sb.append(optsSuffix);
    return sb.toString();
  }
}
