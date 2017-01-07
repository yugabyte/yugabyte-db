package org.yb.loadtester;

import org.yb.loadtester.common.Configuration;

public abstract class Workload {
  public static WorkloadConfig workloadConfig = new WorkloadConfig();

  /**
   * Initialize the workload with various params.
   */
  public abstract void initialize(Configuration configuration, String args);

  /**
   * Create any table if needed.
   */
  public abstract void createTableIfNeeded();

  /**
   * Perform a single read. Throws an exception to signal failure.
   */
  public abstract void doRead();

  /**
   * Perform a single write. Throws an exception to signal failure.
   */
  public abstract void doWrite();

  /**
   * Returns true if the workload has finished running, false otherwise.
   */
  public abstract boolean hasFinished();
}
