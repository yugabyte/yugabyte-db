package com.yugabyte.yw.common.metrics.tsdb;

import java.io.EOFException;
import java.io.IOException;
import java.io.InputStream;

public class InputStreamBitInput extends AbstractBitInput {
  private final InputStream is;

  public InputStreamBitInput(InputStream is) {
    this.is = is;
  }

  @Override
  protected void flipByte() throws IOException {
    if (bitsLeft == 0) {
      int nextByte = is.read();
      if (nextByte == -1) {
        throw new EOFException("End of stream reached while trying to read a byte.");
      }
      b = (byte) nextByte;
      bitsLeft = Byte.SIZE;
    }
  }
}
