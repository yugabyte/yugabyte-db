package org.yb.client;

public class CdcSdkCheckpoint {
  private final long term;
  private final long index;
  private final byte[] key;
  private final int write_id;
  private final long time;

  public CdcSdkCheckpoint(long term, long index, byte[] key, int write_id, long time) {
    this.term = term;
    this.index = index;
    this.key = key;
    this.write_id = write_id;
    this.time = time;
  }

  public long getTerm() {
    return term;
  }

  public long getIndex() {
    return index;
  }

  public byte[] getKey() {
    return key;
  }

  public int getWriteId() {
    return write_id;
  }

  public long getTime() {
    return time;
  }
}
