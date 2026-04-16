package com.yugabyte.yw.common.metrics.tsdb;

import java.io.IOException;
import java.nio.ByteBuffer;

public class ByteBufferBitInput extends AbstractBitInput {
  private final ByteBuffer bb;

  public ByteBufferBitInput(ByteBuffer buf) {
    bb = buf;
  }

  public ByteBufferBitInput(byte[] input) {
    this(ByteBuffer.wrap(input));
  }

  @Override
  public boolean readBit() throws IOException {
    flipByte();
    boolean bit = ((b >> (bitsLeft - 1)) & 1) == 1;
    bitsLeft--;
    return bit;
  }

  @Override
  protected void flipByte() throws IOException {
    if (bitsLeft == 0) {
      if (!bb.hasRemaining()) {
        throw new IOException("Byte buffer underflow");
      }
      b = bb.get(); // Can throw BufferUnderflowException
      bitsLeft = Byte.SIZE;
    }
  }
}
