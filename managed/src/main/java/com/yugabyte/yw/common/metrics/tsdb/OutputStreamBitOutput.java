package com.yugabyte.yw.common.metrics.tsdb;

import java.io.IOException;
import java.io.OutputStream;

public class OutputStreamBitOutput implements BitOutput {
  private final OutputStream os;
  private byte currentByte;
  private int bitPosition; // Position for the next bit (0-7)

  public OutputStreamBitOutput(OutputStream os) {
    this.os = os;
    this.currentByte = 0;
    this.bitPosition = 0;
  }

  @Override
  public void writeBit(boolean bit) throws IOException {
    if (bit) {
      currentByte |= (1 << (7 - bitPosition));
    }
    bitPosition++;
    if (bitPosition == 8) {
      flushByte();
    }
  }

  @Override
  public void writeUVarint64(long value) throws IOException {
    long v = value;
    while (v >= 0x80) {
      writeByte((byte) (v | 0x80));
      v >>= 7;
    }
    writeByte((byte) v);
  }

  @Override
  public void writeLong(long value, int bits) throws IOException {
    if (bits <= 0) {
      return;
    }

    // If not byte-aligned, write bits until it is.
    if (bitPosition != 0) {
      int bitsToWrite = Math.min(bits, 8 - bitPosition);
      // Get the bits to write from the value
      byte shiftedBits = (byte) (value >> (bits - bitsToWrite));
      currentByte |= (shiftedBits & ((1 << bitsToWrite) - 1)) << (8 - bitPosition - bitsToWrite);
      bitPosition += bitsToWrite;
      bits -= bitsToWrite;
      if (bitPosition == 8) flushByte();
    }

    // Write full bytes.
    while (bits >= 8) {
      writeByte((byte) (value >> (bits - 8)));
      bits -= 8;
    }

    // Write remaining bits.
    for (int i = bits - 1; i >= 0; i--) {
      writeBit(((value >> i) & 1) == 1);
    }
  }

  private void writeByte(byte b) throws IOException {
    if (bitPosition == 0) {
      os.write(b);
      return;
    }
    for (int i = 7; i >= 0; i--) {
      writeBit(((b >> i) & 1) == 1);
    }
  }

  @Override
  public void flush() throws IOException {
    // First, flush any partial byte to the buffer.
    if (bitPosition > 0) {
      flushByte();
    }
    os.flush();
  }

  private void flushByte() throws IOException {
    if (bitPosition == 0) {
      return;
    }
    os.write(currentByte);
    currentByte = 0;
    bitPosition = 0;
  }
}
