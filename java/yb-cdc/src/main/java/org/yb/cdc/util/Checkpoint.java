package org.yb.cdc.util;

import com.google.common.base.Objects;
import org.yb.client.GetChangesResponse;

import java.util.Arrays;

public class Checkpoint {
  private long term;
  private long index;
  private byte[] key;
  private int writeId;
  private long snapshotTime;

  public Checkpoint(long term, long index, byte[] key, int writeId, long snapshotTime) {
    this.term = term;
    this.index = index;
    this.key = key;
    this.writeId = writeId;
    this.snapshotTime = snapshotTime;
  }

  public static Checkpoint from(GetChangesResponse resp) {
    return new Checkpoint(resp.getTerm(), resp.getIndex(), resp.getKey(),
                          resp.getWriteId(), resp.getSnapshotTime());
  }

  @Override
  public String toString() {
    return "Checkpoint{" +
      "term=" + term +
      ", index=" + index +
      ", key=" + Arrays.toString(key) +
      ", writeId=" + writeId +
      ", snapshotTime=" + snapshotTime +
      '}';
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) {
      return true;
    }

    if (o == null || getClass() != o.getClass()) {
      return false;
    }

    Checkpoint that = (Checkpoint) o;
    return term == that.getTerm() && index == that.getIndex() && writeId == that.getWriteId()
      && java.util.Objects.equals(key, that.getKey());
  }

  @Override
  public int hashCode() {
    return Objects.hashCode(term, index, key, writeId);
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

  public long getSnapshotTime() {
    return snapshotTime;
  }
}
