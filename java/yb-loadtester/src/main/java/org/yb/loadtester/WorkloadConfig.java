package org.yb.loadtester;

public class WorkloadConfig {
  // The percentage of total threads that perform reads. The rest perform writes. Note that if this
  // value is 100, then no writes will happen. The plugin should have enough information as params
  // to be able to handle this scenario. This value is ignored if set to -1.
  public int readIOPSPercentage;

  // This is the exact number of reader threads.
  public int numReaderThreads;

  // This is the exact number of writer threads.
  public int numWriterThreads;

  // The number of keys to write as a part of this workload.
  public int numKeysToWrite;

  // The number of keys to read as a part of this workload. This ignores the attempts to read where
  // no data has yet to be written.
  public int numKeysToRead;
}
