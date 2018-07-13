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

import redis.clients.jedis.Response;
import redis.clients.jedis.Jedis;
import redis.clients.jedis.Pipeline;
import redis.clients.jedis.Jedis;

import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import java.util.Vector;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.atomic.AtomicLong;
import java.util.zip.Adler32;
import java.util.zip.Checksum;

/**
 * This workload writes and reads some random string keys from a Redis server. One reader and one
 * writer thread each is spawned.
 */
public class RedisPipelinedKeyValue extends RedisKeyValue {
  private static final Logger LOG = Logger.getLogger(RedisPipelinedKeyValue.class);

  ExecutorService flushPool;
  AtomicLong numOpsRespondedThisRound;
  AtomicLong numPipelinesCreated;
  AtomicLong pendingPipelineBatches;
  boolean shouldSleepBeforeNextRound = false;
  // Responses for pipelined redis operations.
  ArrayList<Callable<Integer>> pipelinedOpResponseCallables;

  public RedisPipelinedKeyValue() {
    pipelinedOpResponseCallables = new ArrayList<Callable<Integer>>(appConfig.redisPipelineLength);
    flushPool = Executors.newCachedThreadPool();
    numOpsRespondedThisRound = new AtomicLong();
    numPipelinesCreated = new AtomicLong();
    pendingPipelineBatches = new AtomicLong();
  }

  @Override
  public long doRead() {
    Key key = getSimpleLoadGenerator().getKeyToRead();
    if (key == null) {
      // There are no keys to read yet.
      return 0;
    }
    if (appConfig.valueSize == 0) {
      Response<String> value = doActualReadString(key);
      verifyReadString(key, value);
    } else {
      Response<byte[]> value = doActualReadBytes(key);
      verifyReadBytes(key, value);
    }
    return flushPipelineIfNecessary();
  }

  protected long flushPipelineIfNecessary() {
    if (pipelinedOpResponseCallables.size() % appConfig.redisPipelineLength == 0) {
      ArrayList<Callable<Integer>> callbacksToWaitFor = pipelinedOpResponseCallables;
      long batch = numPipelinesCreated.addAndGet(1);
      Pipeline currPipeline = getRedisPipeline();
      Jedis currJedis = getJedisClient();
      if (appConfig.sleepTime == 0) {  // 0 disables running pipelines in parallel.
        doActualFlush(currJedis, currPipeline, callbacksToWaitFor, batch, false);
      } else {
        flushPool.submit(new Runnable() {
          public void run() {
            doActualFlush(currJedis, currPipeline, callbacksToWaitFor, batch, true);
          }
        });
        LOG.debug("Submitted a runnable. Now resetting clients");
        resetClients();
        shouldSleepBeforeNextRound = true;
      }
      pipelinedOpResponseCallables =
          new ArrayList<Callable<Integer>>(appConfig.redisPipelineLength);
    }

    return numOpsRespondedThisRound.getAndSet(0);
  }

  public void performWrite() {
    super.performWrite();
    sleepIfNeededBeforeNextBatch();
  }

  public void performRead() {
    super.performRead();
    sleepIfNeededBeforeNextBatch();
  }

  private void sleepIfNeededBeforeNextBatch() {
    if (shouldSleepBeforeNextRound) {
      try {
        LOG.debug("Sleeping for " + appConfig.sleepTime + " ms");
        Thread.sleep(appConfig.sleepTime);
      } catch (Exception e) {
        LOG.error("Caught exception while sleeping ... ", e);
      }
      shouldSleepBeforeNextRound = false;
    }
  }

  protected void doActualFlush(Jedis jedis, Pipeline pipeline,
                               ArrayList<Callable<Integer>> pipelineCallables,
                               long batch, boolean closeAfterFlush) {
    LOG.debug("Flushing pipeline. batch " + batch + " size = " +
              pipelineCallables.size() + " pending batches " +
              pendingPipelineBatches.addAndGet(1));
    int count = 0;
    try {
      pipeline.sync();
      for (Callable<Integer> c : pipelineCallables) {
        count += c.call();
      }
      if (closeAfterFlush) {
        pipeline.close();
        jedis.close();
      }
    } catch (Exception e) {
      throw new RuntimeException(
        "Caught Exception from redis pipeline " + getRedisServerInUse(), e);
    } finally {
      pipelineCallables.clear();
      numOpsRespondedThisRound.addAndGet(count);
    }
    LOG.debug("Processed batch  " + batch + " count " + count + " responses."
              + " pending batches " + pendingPipelineBatches.addAndGet(-1));
  }

  public Response<String> doActualReadString(Key key) {
    return getRedisPipeline().get(key.asString());
  }

  public Response<byte[]> doActualReadBytes(Key key) {
    return getRedisPipeline().get(key.asString().getBytes());
  }

  private void verifyReadString(final Key key, final Response<String> value) {
    pipelinedOpResponseCallables.add(new Callable<Integer>() {
      @Override
      public Integer call() throws Exception {
        key.verify(value.get());
        return 1;
      }
    });
  }

  private void verifyReadBytes(final Key key, final Response<byte[]> value) {
    pipelinedOpResponseCallables.add(new Callable<Integer>() {
      @Override
      public Integer call() throws Exception {
        verifyRandomValue(key, value.get());
        return 1;
      }
    });
  }

  @Override
  public long doWrite() {
    Key key = getSimpleLoadGenerator().getKeyToWrite();
    if (key != null) {
      Response<String> retVal = doActualWrite(key);
      verifyWriteResult(key, retVal);
      return flushPipelineIfNecessary();
    } else {
      return 0;
    }
  }

  public Response<String> doActualWrite(Key key) {
    Response<String> retVal;
    if (appConfig.valueSize == 0) {
      String value = key.getValueStr();
      retVal = getRedisPipeline().set(key.asString(), value);
    } else {
      retVal = getRedisPipeline().set(key.asString().getBytes(),
                                      getRandomValue(key));
    }
    return retVal;
  }

  private long verifyWriteResult(final Key key, final Response<String> retVal) {
    pipelinedOpResponseCallables.add(new Callable<Integer>() {
      @Override
      public Integer call() throws Exception {
        if (retVal.get() == null) {
          getSimpleLoadGenerator().recordWriteFailure(key);
          return 0;
        }
        LOG.debug("Wrote key: " + key.toString() + ", return code: " +
                  retVal.get());
        getSimpleLoadGenerator().recordWriteSuccess(key);
        return 1;
      }
    });
    return 1;
  }

  @Override
  public List<String> getExampleUsageOptions() {
    Vector<String> usage = new Vector<String>(super.getExampleUsageOptions());
    usage.add("--pipeline_length " + appConfig.redisPipelineLength);
    return usage;
  }
}
