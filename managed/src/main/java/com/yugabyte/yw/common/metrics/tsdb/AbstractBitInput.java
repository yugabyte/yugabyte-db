package com.yugabyte.yw.common.metrics.tsdb;

import java.io.IOException;

public abstract class AbstractBitInput implements BitInput {
  protected static final int MAX_VARINT64_LENGTH = 10;
  protected byte b;
  protected int bitsLeft = 0;

  @Override
  public boolean readBit() throws IOException {
    flipByte();
    boolean bit = ((b >> (bitsLeft - 1)) & 1) == 1;
    bitsLeft--;
    return bit;
  }

  @Override
  public long readLong(int bits) throws IOException {
    long value = 0;
    while (bits > 0) {
      flipByte();
      if (bits > bitsLeft || bits == Byte.SIZE) {
        // Take only the bitsLeft "least significant" bits
        byte d = (byte) (b & ((1 << bitsLeft) - 1));
        value = (value << bitsLeft) + (d & 0xFF);
        bits -= bitsLeft;
        bitsLeft = 0;
      } else {
        // Shift to correct position and take only least significant bits
        byte d = (byte) ((b >>> (bitsLeft - bits)) & ((1 << bits) - 1));
        value = (value << bits) + (d & 0xFF);
        bitsLeft -= bits;
        bits = 0;
      }
    }
    return value;
  }

  @Override
  public long readUVarint64() throws IOException {
    long result = 0;
    int shift = 0;
    for (int i = 0; ; i++) {
      long curByte = readLong(8);
      if (curByte < 0x80) {
        if (i == MAX_VARINT64_LENGTH - 1 && curByte > 1) {
          throw new IOException("Unexpected 10th byte value in UVarint64");
        }
        return result | curByte << shift;
      }
      if (i >= MAX_VARINT64_LENGTH) {
        throw new IOException("Varint is too long");
      }
      result |= (curByte & 0x7fL) << shift;
      shift += 7;
    }
  }

  @Override
  public long readVarint64() throws IOException {
    long uvarint = readUVarint64();
    long result = uvarint >> 1;
    if ((uvarint & 1L) != 0L) {
      result = ~result;
    }
    return result;
  }

  protected abstract void flipByte() throws IOException;
}
