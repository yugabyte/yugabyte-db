// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.logging;

import java.util.concurrent.BlockingQueue;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;

public class MDCAwareThreadPoolExecutor extends ThreadPoolExecutor {

  public MDCAwareThreadPoolExecutor(
      int corePoolSize,
      int maximumPoolSize,
      long keepAliveTime,
      TimeUnit unit,
      BlockingQueue<Runnable> workQueue,
      ThreadFactory namedThreadFactory) {
    super(corePoolSize, maximumPoolSize, keepAliveTime, unit, workQueue, namedThreadFactory);
  }

  @Override
  public void execute(Runnable runnable) {
    super.execute(new MDCAwareRunnable(runnable));
  }
}
