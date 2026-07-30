package com.yugabyte.yw.common.metrics.tsdb;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.apache.commons.lang3.tuple.Pair;

public class ChunkDecompressor {
  private int numTotal;

  private long timestamp;
  private long value;

  private int leading;
  private int trailing;

  private long tDelta;

  private final BitInput in;

  public ChunkDecompressor(BitInput input) throws IOException {
    this.in = input;
    readHeader();
  }

  private void readHeader() throws IOException {
    numTotal = (int) in.readLong(16);
  }

  public List<Pair<Long, Double>> readPoints() throws IOException {
    List<Pair<Long, Double>> result = new ArrayList<>(numTotal);
    for (int i = 0; i < numTotal; i++) {
      if (i == 0) {
        timestamp = in.readVarint64();
        value = in.readLong(64);
      } else if (i == 1) {
        tDelta = in.readUVarint64();
        timestamp = timestamp + tDelta;
        value = readValue();
      } else {
        byte d = 0;
        for (int j = 0; j < 4; j++) {
          d <<= 1;
          boolean bit = in.readBit();
          if (!bit) {
            break;
          }
          d |= 1;
        }
        int size = 0;
        long dod = 0L;
        switch (d) {
          case 0b0:
            // dod == 0
            break;
          case 0b10:
            size = 14;
            break;
          case 0b110:
            size = 17;
            break;
          case 0b1110:
            size = 20;
            break;
          case 0b1111:
            // Do not use fast because it's very unlikely it will succeed.
            dod = in.readLong(64);
            break;
        }

        if (size != 0) {
          dod = in.readLong(size);
          if (dod > (1 << (size - 1))) {
            // or something
            dod = dod - (1 << size);
          }
        }

        tDelta = tDelta + dod;
        timestamp = timestamp + tDelta;
        value = readValue();
      }
      result.add(ImmutablePair.of(timestamp, Double.longBitsToDouble(value)));
    }
    return result;
  }

  private long readValue() throws IOException {
    boolean bit = in.readBit();
    if (bit) {
      bit = in.readBit();
      if (!bit) {
        // reuse leading/trailing zero bits
      } else {
        leading = (int) in.readLong(5);
        int significantBits = (int) in.readLong(6);
        if (significantBits == 0) {
          significantBits = 64;
        }
        trailing = (int) (64 - leading - significantBits);
      }
      int bitsToRead = 64 - leading - trailing;
      long bits = in.readLong(bitsToRead);
      value ^= (bits << trailing);
    }
    return value;
  }
}
