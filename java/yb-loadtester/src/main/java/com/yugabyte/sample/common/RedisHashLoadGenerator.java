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

package com.yugabyte.sample.common;

import java.util.ArrayList;

import org.apache.commons.math3.distribution.AbstractIntegerDistribution;
import org.apache.commons.math3.distribution.UniformIntegerDistribution;
import org.apache.commons.math3.distribution.ZipfDistribution;
import org.apache.log4j.Logger;

import com.yugabyte.sample.common.SimpleLoadGenerator.Key;

public class RedisHashLoadGenerator {
  private static final Logger LOG = Logger.getLogger(RedisHashLoadGenerator.class);

  final String prefix;
  final int numKeys, numSubKeys, numSubkeysPerRead, numSubkeysPerWrite;
  SimpleLoadGenerator loadGenerator;

  // Initially set to false. Once all the data is loaded, subkeys will be selected at random.
  boolean selectRandomly = false;
  AbstractIntegerDistribution keyFreqDist, subkeyFreqDist;
  private static final double keyZipfExpThreshold = 1.5;
  private static final double subkeyZipfExpThreshold = 5;


  public static class KeySubKey {
    SimpleLoadGenerator.Key key;
    SimpleLoadGenerator.Key subkey;

    public KeySubKey(long key, int subkey, String prefix) {
      this.key = new SimpleLoadGenerator.Key(key, prefix);
      this.subkey = new SimpleLoadGenerator.Key(subkey, "");
    }

    public SimpleLoadGenerator.Key getKey() { return key; }

    public SimpleLoadGenerator.Key getSubkey() { return subkey; }
    public long keyAsNumber() { return key.asNumber(); }

    public int subkeyAsNumber() { return (int)subkey.asNumber(); }

    public String asString() {
      return key.asString() + "#" + subkey.asNumber();
    }

    public String getValueStr() {
      return ("val:" + key.asNumber() + "#" + subkey.asNumber());
    }

    public void verify(String value) {
      if (value == null || !value.equals(getValueStr())) {
        LOG.fatal("Value mismatch for key#subkey: " + key.asNumber() + "#" +
                  subkey.asNumber() + ", expected: " + getValueStr() +
                  ", got: " + value);
      }
    }

    @Override
    public String toString() {
      return "Key: " + key.asNumber() + ", subkey: " + subkey.asNumber() +
          ", value: " + getValueStr();
    }
  }

  public RedisHashLoadGenerator(String prefix,
      int numKeys, int numSubKeys,
      double keyZipfExp, double subkeyZipfExp,
      int numSubkeysPerWrite, int numSubkeysPerRead) {
    this.prefix = prefix;
    this.numKeys = numKeys;
    this.keyFreqDist =
        (keyZipfExp > 0 ? new ZipfDistribution(numKeys, keyZipfExp)
                     : new UniformIntegerDistribution(1, numKeys));
    printInfoAboutZifpian(numKeys, keyZipfExp, keyZipfExpThreshold);
    this.numSubKeys = numSubKeys;
    this.subkeyFreqDist =
        (subkeyZipfExp > 0 ? new ZipfDistribution(numSubKeys, subkeyZipfExp)
                     : new UniformIntegerDistribution(1, numSubKeys));
    printInfoAboutZifpian(numSubKeys, subkeyZipfExp, subkeyZipfExpThreshold);
    this.numSubkeysPerRead = numSubkeysPerRead;
    this.numSubkeysPerWrite = numSubkeysPerWrite;
    int numWrites = numKeys * (int)Math.ceil((double) numSubKeys / numSubkeysPerWrite);
    // Generates 0 ... (numWrites - 1).
    this.loadGenerator = new SimpleLoadGenerator(0, numWrites, -1);
  }

  public AbstractIntegerDistribution getSubkeyDistribution() {
    return subkeyFreqDist;
  }

  private boolean selectRandomly() {
    if (!selectRandomly) {
      selectRandomly = !loadGenerator.stillLoading();
      if (selectRandomly) {
        LOG.info(
            "Loaded all the data. Will now start choosing subkeys at random");
      }
    }
    return selectRandomly;
  }

  public ArrayList<KeySubKey> getRandomKeySubkeys(int n) {
    long key = keyFreqDist.sample();
    int[] randomSubKeys = subkeyFreqDist.sample(n);
    ArrayList<KeySubKey> ret = new ArrayList<KeySubKey>();
    for (int cnt = 0; cnt < n; cnt++) {
      ret.add(new KeySubKey(key, randomSubKeys[cnt], prefix));
    }
    return ret;
  }

  private KeySubKey getMaxKeySubKeyForWriteBatch(int wb) {
    long key = 1 + wb % numKeys;
    int write_batch_for_key = 1 + (wb / numKeys);
    int max_subkey = Math.min(numSubkeysPerWrite * write_batch_for_key, numSubKeys);
    return new KeySubKey(key, max_subkey, prefix);

  }

  private Key getWriteBatchForKeySubkeys(ArrayList<KeySubKey> ks) {
    // ks[0] is the max subkey in the write batch.
    int write_batch_for_key = (int) Math.ceil(
        (double) ks.get(0).subkeyAsNumber() / numSubkeysPerWrite);
    return new Key(ks.get(0).keyAsNumber() - 1 + numKeys * (write_batch_for_key - 1), prefix);
  }

  /*
   * Each batch of write will write <numSubkeysPerWrite> subkeys for a particular
   * key. We have a total of numKeys * Math.ceil(numSubkeysPerKey / numSubkeysPerWrite)
   * batches. Returns the next <numSubkeysPerWrite> subkeys to be updated for the
   * next key.
   */
  public ArrayList<KeySubKey> getWriteBatch() {
    int batch = (int) loadGenerator.getKeyToWrite().asNumber();
    KeySubKey max = getMaxKeySubKeyForWriteBatch(batch);
    long key = max.keyAsNumber();
    int max_subkey = max.subkeyAsNumber();
    ArrayList<KeySubKey> ret = new ArrayList<KeySubKey>();
    for (int i = 0; i < numSubkeysPerWrite; i++) {
      ret.add(new KeySubKey(key, max_subkey - i, prefix));
    }
    return ret;
  }

  /*
   * Each batch of read will write <numSubkeysPerRead> subkeys for a particular
   * key.
   *
   * To ensure that we only read key/subkeys that have been written already, we
   * first figure out a write batch up to which we can read, and use it to calculate
   * the key/subkeys that are to be read.
   */
  public ArrayList<KeySubKey> getReadBatch() {
    Key readBatchKey = loadGenerator.getKeyToRead();
    // If we haven't written any batches yet, return null.
    if (readBatchKey == null) return null;

    KeySubKey max = getMaxKeySubKeyForWriteBatch((int) readBatchKey.asNumber());
    long key = max.keyAsNumber();
    int max_subkey = max.subkeyAsNumber();

    // pick numSubkeysPerRead random keys between 1 and max_subkey.
    int[] randomSubKeys = subkeyFreqDist.sample(numSubkeysPerRead);
    ArrayList<KeySubKey> ret = new ArrayList<KeySubKey>();
    for (int i = 0; i < numSubkeysPerWrite; i++) {
      ret.add(new KeySubKey(key, 1 + (randomSubKeys[i] % max_subkey), prefix));
    }
    return ret;
  }

  public ArrayList<KeySubKey> getKeySubkeysToWrite() {
    if (selectRandomly()) {
      return getRandomKeySubkeys(this.numSubkeysPerWrite);
    } else {
      return getWriteBatch();
    }
  }

  public ArrayList<KeySubKey> getKeySubkeysToRead() {
    if (selectRandomly()) {
      return getRandomKeySubkeys(this.numSubkeysPerRead);
    } else {
      return getReadBatch();
    }
  }


  public void recordWriteFailure(ArrayList<KeySubKey> ks) {
    Key simpleKey = getWriteBatchForKeySubkeys(ks);
    LOG.debug("Marking Write Failure for write batch " + simpleKey);
    loadGenerator.recordWriteFailure(simpleKey);
  }


  public void recordWriteSuccess(ArrayList<KeySubKey> ks) {
    Key simpleKey = getWriteBatchForKeySubkeys(ks);
    LOG.debug("Marking Write Success for write batch " + simpleKey);
    loadGenerator.recordWriteSuccess(simpleKey);
  }

  private static void printInfoAboutZifpian(int numElements, double zifpExp, double warnThreshold) {
    if (zifpExp <= 0) {
      return;
    }
    if (zifpExp >= warnThreshold) {
      LOG.warn("Zipf exponent of " + zifpExp + " may cause the probabilities to decay too fast.");
    }
    LOG.info("Printing distribution for Zifpian n = " + numElements + " exp = " + zifpExp);
    ZipfDistribution zipf = new ZipfDistribution(numElements, zifpExp);
    int step = Math.max(1, numElements / 10);
    for (int i = step; i <= numElements; i += step) {
      LOG.info("p[ x <= " + i + "] = " + zipf.cumulativeProbability(i));
    }
  }
}
