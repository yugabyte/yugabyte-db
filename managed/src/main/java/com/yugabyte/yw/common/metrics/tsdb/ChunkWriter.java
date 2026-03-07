package com.yugabyte.yw.common.metrics.tsdb;

import java.io.IOException;
import java.util.List;
import org.apache.commons.lang3.tuple.Pair;

public class ChunkWriter {
  private final BitOutput out;
  private long prevTimestamp;
  private long prevValue;
  private long prevTDelta;

  private int prevLeading;
  private int prevTrailing;

  public ChunkWriter(OutputStreamBitOutput out) {
    this.out = out;
  }

  public void write(List<Pair<Long, Double>> points) throws IOException {
    if (points == null || points.isEmpty()) {
      throw new RuntimeException("points can't be empty");
    }

    // Write header: 16 bits for the number of points.
    out.writeLong(points.size(), 16);

    for (int i = 0; i < points.size(); i++) {
      Pair<Long, Double> point = points.get(i);
      long currentTimestamp = point.getKey();
      long currentValue = Double.doubleToRawLongBits(point.getValue());

      if (i == 0) {
        // First point: write full timestamp and value.
        out.writeVarint64(currentTimestamp);
        out.writeLong(currentValue, 64);
      } else if (i == 1) {
        // Second point: write delta for timestamp.
        long tDelta = currentTimestamp - prevTimestamp;
        out.writeUVarint64(tDelta);
        writeValue(currentValue);
        prevTDelta = tDelta;
      } else {
        // Subsequent points: write delta-of-delta for timestamp.
        long tDelta = currentTimestamp - prevTimestamp;
        long dod = tDelta - prevTDelta;

        if (dod == 0) {
          out.writeBit(false);
        } else if (dod >= -63 && dod <= 64) { // Prometheus uses -63 to 64 for 14 bits
          out.writeLong(0b10, 2);
          out.writeLong(dod, 14);
        } else if (dod >= -8191 && dod <= 8192) { // Prometheus uses -8191 to 8192 for 17 bits
          out.writeLong(0b110, 3);
          out.writeLong(dod, 17);
        } else if (dod >= -131071
            && dod <= 131072) { // Prometheus uses -131071 to 131072 for 20 bits
          out.writeLong(0b1110, 4);
          out.writeLong(dod, 20);
        } else {
          out.writeLong(0b1111, 4);
          out.writeLong(dod, 64);
        }

        writeValue(currentValue);
        prevTDelta = tDelta;
      }

      prevTimestamp = currentTimestamp;
      prevValue = currentValue;
    }
  }

  private void writeValue(long currentValue) throws IOException {
    long xor = prevValue ^ currentValue;

    if (xor == 0) {
      // Value is the same, write a single '0' bit.
      out.writeBit(false);
      return;
    }

    // Value is different, write a '1' bit.
    out.writeBit(true);

    int leading = Long.numberOfLeadingZeros(xor);
    int trailing = Long.numberOfTrailingZeros(xor);

    if (leading >= prevLeading && trailing >= prevTrailing) {
      // Control bit '0': Use previous leading/trailing info.
      out.writeBit(false);
      int significantBits = 64 - prevLeading - prevTrailing;
      out.writeLong(xor >>> prevTrailing, significantBits);
    } else {
      // Control bit '1': Write new leading/trailing info.
      out.writeBit(true);

      int significantBits = 64 - leading - trailing;

      // Write leading zeros count (5 bits).
      out.writeLong(leading, 5);

      // Write length of significant bits (6 bits).
      // A value of 0 means 64 significant bits.
      if (significantBits == 64) {
        out.writeLong(0, 6);
      } else {
        out.writeLong(significantBits, 6);
      }

      // Write the significant bits themselves.
      out.writeLong(xor >>> trailing, significantBits);

      // Store for next iteration.
      prevLeading = leading;
      prevTrailing = trailing;
    }
  }

  /**
   * Flushes any remaining bits in the buffer to the underlying output stream. Call this when you
   * are done writing.
   *
   * @throws IOException if an I/O error occurs.
   */
  public void flush() throws IOException {
    out.flush();
  }
}
