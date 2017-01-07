package org.yb.loadtester;

public class WorkloadConfig {
  // A description of the workload.
  public String description;

  // The percentage of total threads that perform reads. The rest perform writes. Note that if this
  // value is 100, then no writes will happen. The plugin should have enough information as params
  // to be able to handle this scenario. This value is ignored if set to -1.
  public int readIOPSPercentage;

  // This is the exact number of reader threads.
  public int numReaderThreads;

  // This is the exact number of writer threads.
  public int numWriterThreads;

}
