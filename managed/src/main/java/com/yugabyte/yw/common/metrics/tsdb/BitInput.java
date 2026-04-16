package com.yugabyte.yw.common.metrics.tsdb;

import java.io.IOException;

public interface BitInput {
  boolean readBit() throws IOException;

  long readLong(int bits) throws IOException;

  long readUVarint64() throws IOException;

  long readVarint64() throws IOException;
}
