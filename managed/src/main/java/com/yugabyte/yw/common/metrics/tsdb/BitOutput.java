package com.yugabyte.yw.common.metrics.tsdb;

import java.io.IOException;

public interface BitOutput {

  void writeBit(boolean bit) throws IOException;

  void writeLong(long value, int bits) throws IOException;

  default void writeVarint64(long value) throws IOException {
    long uvarint = value << 1;
    if (uvarint < 0) {
      uvarint = ~uvarint;
    }
    writeUVarint64(uvarint);
  }

  void writeUVarint64(long value) throws IOException;

  void flush() throws IOException;
}
