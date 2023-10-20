package org.yb.client;

import java.util.Arrays;

/**
 * Class to represent the checkpoint for the CDCSDK service.
 * Containes term, index, key, writeId and time as protected fields, with getters.
 */
public class CdcSdkCheckpoint {
  protected final long term;
  protected final long index;
  protected final byte[] key;
  protected final int writeId;
  protected long time;

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
