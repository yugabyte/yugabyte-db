package org.yb.client;

import java.util.Arrays;

public class CdcSdkCheckpoint {
  private final long term;
  private final long index;
  private final byte[] key;
  private final int writeId;
  private final long time;

  public CdcSdkCheckpoint(long term, long index, byte[] key, int writeId, long time) {
    this.term = term;
    this.index = index;
    this.key = key;
    this.writeId = writeId;
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
    return writeId;
  }

  public long getTime() {
    return time;
  }

  @Override
  public String toString() {
    return String.format("%d.%d.%s.%d.%d", this.term, this.index, Arrays.toString(key),
                         this.writeId, this.time);
  }
}
