// Copyright (c) YugaByte, Inc.

package com.yugabyte.sample.apps;

/**
 * This class encapsulates the various configuration parameters for the various apps.
 */
public class AppConfig {

  /**
   * The type of the current app. The valid types are:
   *   - OLTP   : These apps implement read() and write() methods. The read() and write() methods
   *              are called from independent threads. There is a metrics tracker that tracks
   *              latency and throughput.
   *   - Simple : These apps just implement a run() method. No metrics tracking is done.
   */
  public static enum Type {
    OLTP,
    Simple
  }
  // The app type, default initialized to OLTP.
  public Type appType = Type.OLTP;


  // The percentage of total threads that perform reads. The rest perform writes. Note that if this
  // value is 100, then no writes will happen. The plugin should have enough information as params
  // to be able to handle this scenario. This value is ignored if set to -1.
  public int readIOPSPercentage;

  // This is the exact number of reader threads.
  public int numReaderThreads;

  // This is the exact number of writer threads.
  public int numWriterThreads;

  // The number of keys to write as a part of this workload.
  public long numKeysToWrite;

  // The number of keys to read as a part of this workload. This ignores the attempts to read where
  // no data has yet to be written.
  public long numKeysToRead;

  // The size of the value to be written.
  public int valueSize;

  // The number of unique keys to write, once these are written, the subsequent writes will be
  // updates to existing keys.
  public long numUniqueKeysToWrite;

  // Maximum written key in case we reuse existing table.
  public long maxWrittenKey = -1;

  // The table level TTL in seconds. No TTL is applied if this value is set to -1.
  public long tableTTLSeconds = -1;
}
