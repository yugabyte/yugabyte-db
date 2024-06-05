/*
 * Copyright (C) 2010-2012  The Async HBase Authors.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *   - Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   - Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *   - Neither the name of the StumbleUpon nor the names of its contributors
 *     may be used to endorse or promote products derived from this software
 *     without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
package org.yb.client;

import io.netty.buffer.ByteBuf;
import io.netty.util.CharsetUtil;
import org.yb.annotations.InterfaceAudience;
import org.yb.util.Slice;

import java.io.DataInput;
import java.io.DataOutput;
import java.io.IOException;
import java.math.BigInteger;
import java.nio.ByteBuffer;
import java.util.*;

/**
 * Helper functions to manipulate byte arrays.
 */
@InterfaceAudience.Private
public final class Bytes {

  // Two's complement reference: 2^n .
  // In this case, 2^64 (so as to emulate a unsigned long)
  // from http://stackoverflow.com/questions/10886962/interpret-a-negative-number-as-unsigned-with-
  // biginteger-java
  private static final BigInteger TWO_COMPL_REF = BigInteger.ONE.shiftLeft(64);

  private Bytes() {  // Can't instantiate.
  }

  // -------------------------------- //
  // Byte array conversion utilities. //
  // -------------------------------- //

  /**
   * Reads a boolean from the beginning of the given array.
   * @param b The array to read from.
   * @return A boolean
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static boolean getBoolean(final byte[] b) {
    byte v = getByte(b, 0);
    return v == 1;
  }

  /**
   * Reads a boolean from an offset in the given array.
   * @param b The array to read from.
   * @param offset The offset into the array.
   * @return A boolean
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static boolean getBoolean(final byte[] b, final int offset) {
    byte v = getByte(b, offset);
    return v == 1;
  }

  /**
   * Reads a byte from the beginning of the given array.
   * @param b The array to read from.
   * @return A byte
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static byte getByte(final byte[] b) {
    return getByte(b, 0);
  }

  /**
   * Reads a byte from an offset in the given array.
   * @param b The array to read from.
   * @return A byte
   * @return
   */
  public static byte getByte(final byte[] b, final int offset) {
    return b[offset];
  }

  /**
   * Reads an unsigned byte from the beginning of the given array.
   * @param b The array to read from.
   * @return A positive byte
   */
  public static short getUnsignedByte(final byte[] b) {
    return getUnsignedByte(b, 0);
  }

  /**
   * Reads an unsigned byte from an offset in the given array.
   * @param b The array to read from.
   * @return A positive byte
   */
  public static short getUnsignedByte(final byte[] b, final int offset) {
    return (short) (b[offset] & 0x00FF);
  }

  /**
   * Writes an unsigned byte at the beginning of the given array.
   * @param b The array to write to.
   * @param n An unsigned byte.
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static void setUnsignedByte(final byte[] b, final short n) {
    setUnsignedByte(b, n, 0);
  }

  /**
   * Writes an unsigned byte at an offset in the given array.
   * @param b The array to write to.
   * @param offset The offset in the array to start writing at.
   * @param n An unsigned byte.
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static void setUnsignedByte(final byte[] b, final short n,
                                      final int offset) {
    b[offset] = (byte) n;
  }

  /**
   * Creates a new byte array containing an unsigned byte.
   * @param n An unsigned byte.
   * @return A new byte array containing the given value.
   */
  public static byte[] fromUnsignedByte(final short n) {
    final byte[] b = new byte[1];
    setUnsignedByte(b, n);
    return b;
  }

  /**
   * Reads a little-endian 2-byte short from the beginning of the given array.
   * @param b The array to read from.
   * @return A short integer.
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static short getShort(final byte[] b) {
    return getShort(b, 0);
  }

  /**
   * Reads a little-endian 2-byte short from an offset in the given array.
   * @param b The array to read from.
   * @param offset The offset in the array to start reading from.
   * @return A short integer.
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static short getShort(final byte[] b, final int offset) {
    return (short) (b[offset] & 0xFF | b[offset + 1] << 8 );
  }

  /**
   * Reads a little-endian 2-byte unsigned short from the beginning of the
   * given array.
   * @param b The array to read from.
   * @return A positive short integer.
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static int getUnsignedShort(final byte[] b) {
    return getUnsignedShort(b, 0);
  }

  /**
   * Reads a little-endian 2-byte unsigned short from an offset in the
   * given array.
   * @param b The array to read from.
   * @param offset The offset in the array to start reading from.
   * @return A positive short integer.
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static int getUnsignedShort(final byte[] b, final int offset) {
    return getShort(b, offset) & 0x0000FFFF;
  }

  /**
   * Writes a little-endian 2-byte short at the beginning of the given array.
   * @param b The array to write to.
   * @param n A short integer.
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static void setShort(final byte[] b, final short n) {
    setShort(b, n, 0);
  }

  /**
   * Writes a little-endian 2-byte short at an offset in the given array.
   * @param b The array to write to.
   * @param offset The offset in the array to start writing at.
   * @param n A short integer.
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static void setShort(final byte[] b, final short n,
                              final int offset) {
    b[offset + 0] = (byte) (n >>> 0);
    b[offset + 1] = (byte) (n >>> 8);
  }

  /**
   * Writes a little-endian 2-byte unsigned short at the beginning of the given array.
   * @param b The array to write to.
   * @param n An unsigned short integer.
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static void setUnsignedShort(final byte[] b, final int n) {
    setUnsignedShort(b, n, 0);
  }

  /**
   * Writes a little-endian 2-byte unsigned short at an offset in the given array.
   * @param b The array to write to.
   * @param offset The offset in the array to start writing at.
   * @param n An unsigned short integer.
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static void setUnsignedShort(final byte[] b, final int n,
                              final int offset) {
    b[offset + 0] = (byte) (n >>> 0);
    b[offset + 1] = (byte) (n >>> 8);
  }

  /**
   * Creates a new byte array containing a little-endian 2-byte short integer.
   * @param n A short integer.
   * @return A new byte array containing the given value.
   */
  public static byte[] fromShort(final short n) {
    final byte[] b = new byte[2];
    setShort(b, n);
    return b;
  }

  /**
   * Creates a new byte array containing a little-endian 2-byte unsigned short integer.
   * @param n An unsigned short integer.
   * @return A new byte array containing the given value.
   */
  public static byte[] fromUnsignedShort(final int n) {
    final byte[] b = new byte[2];
    setUnsignedShort(b, n);
    return b;
  }

  /**
   * Reads a little-endian 4-byte integer from the beginning of the given array.
   * @param b The array to read from.
   * @return An integer.
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static int getInt(final byte[] b) {
    return getInt(b, 0);
  }

  /**
   * Reads a little-endian 4-byte integer from an offset in the given array.
   * @param b The array to read from.
   * @param offset The offset in the array to start reading from.
   * @return An integer.
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static int getInt(final byte[] b, final int offset) {
    return (b[offset + 0] & 0xFF) << 0
        | (b[offset + 1] & 0xFF) << 8
        | (b[offset + 2] & 0xFF) << 16
        | (b[offset + 3] & 0xFF) << 24;
  }

  /**
   * Reads a little-endian 4-byte unsigned integer from the beginning of the
   * given array.
   * @param b The array to read from.
   * @return A positive integer.
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static long getUnsignedInt(final byte[] b) {
    return getUnsignedInt(b, 0);
  }

  /**
   * Reads a little-endian 4-byte unsigned integer from an offset in the
   * given array.
   * @param b The array to read from.
   * @param offset The offset in the array to start reading from.
   * @return A positive integer.
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static long getUnsignedInt(final byte[] b, final int offset) {
    return getInt(b, offset) & 0x00000000FFFFFFFFL;
  }

  /**
   * Writes a little-endian 4-byte int at the beginning of the given array.
   * @param b The array to write to.
   * @param n An integer.
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static void setInt(final byte[] b, final int n) {
    setInt(b, n, 0);
  }

  /**
   * Writes a little-endian 4-byte int at an offset in the given array.
   * @param b The array to write to.
   * @param offset The offset in the array to start writing at.
   * @param n An integer.
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static void setInt(final byte[] b, final int n, final int offset) {
    b[offset + 0] = (byte) (n >>> 0);
    b[offset + 1] = (byte) (n >>> 8);
    b[offset + 2] = (byte) (n >>>  16);
    b[offset + 3] = (byte) (n >>>  24);
  }

  /**
   * Writes a little-endian 4-byte unsigned int at the beginning of the given array.
   * @param b The array to write to.
   * @param n An unsigned integer.
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static void setUnsignedInt(final byte[] b, final long n) {
    setUnsignedInt(b, n, 0);
  }

  /**
   * Writes a little-endian 4-byte unsigned int at an offset in the given array.
   * @param b The array to write to.
   * @param offset The offset in the array to start writing at.
   * @param n An unsigned integer.
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static void setUnsignedInt(final byte[] b, final long n, final int offset) {
    b[offset + 0] = (byte) (n >>> 0);
    b[offset + 1] = (byte) (n >>> 8);
    b[offset + 2] = (byte) (n >>>  16);
    b[offset + 3] = (byte) (n >>>  24);
  }

  public static void putVarInt32(final ByteBuffer b, final int v) {
    int B = 128;
    if (v < (1<<7)) {
      b.put((byte)v);
    } else if (v < (1<<14)) {
      b.put((byte)(v | B));
      b.put((byte)((v>>7) | B));
    } else if (v < (1<<21)) {
      b.put((byte)(v | B));
      b.put((byte)((v>>7) | B));
      b.put((byte)(v>>14));
    } else if (v < (1<<28)) {
      b.put((byte)(v | B));
      b.put((byte)((v>>7) | B));
      b.put((byte)((v>>14) | B));
      b.put((byte)(v>>21));
    } else {
      b.put((byte)(v | B));
      b.put((byte)((v>>7) | B));
      b.put((byte)((v>>14) | B));
      b.put((byte)((v>>21) | B));
      b.put((byte)(v>>28));
    }
  }

  /**
   * Reads a 32-bit variable-length integer value as used in Protocol Buffers.
   * @param buf The buffer to read from.
   * @return The integer read.
   */
  static int readVarInt32(final ByteBuf buf) {
    int result = buf.readByte();
    if (result >= 0) {
      return result;
    }
    result &= 0x7F;
    result |= buf.readByte() << 7;
    if (result >= 0) {
      return result;
    }
    result &= 0x3FFF;
    result |= buf.readByte() << 14;
    if (result >= 0) {
      return result;
    }
    result &= 0x1FFFFF;
    result |= buf.readByte() << 21;
    if (result >= 0) {
      return result;
    }
    result &= 0x0FFFFFFF;
    final byte b = buf.readByte();
    result |= b << 28;
    if (b >= 0) {
      return result;
    }
    throw new IllegalArgumentException("Not a 32 bit varint: " + result
        + " (5th byte: " + b + ")");
  }

  public static byte[] fromBoolean(final boolean n) {
     final byte[] b = new byte[1];
     b[0] = (byte) (n ? 1 : 0);
     return b;
  }

  /**
   * Creates a new byte array containing a little-endian 4-byte integer.
   * @param n An integer.
   * @return A new byte array containing the given value.
   */
  public static byte[] fromInt(final int n) {
    final byte[] b = new byte[4];
    setInt(b, n);
    return b;
  }

  /**
   * Creates a new byte array containing a little-endian 4-byte unsigned integer.
   * @param n An unsigned integer.
   * @return A new byte array containing the given value.
   */
  public static byte[] fromUnsignedInt(final long n) {
    final byte[] b = new byte[4];
    setUnsignedInt(b, n);
    return b;
  }

  /**
   * Reads a little-endian 8-byte unsigned long from the beginning of the given array.
   * @param b The array to read from.
   * @return A long integer.
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static BigInteger getUnsignedLong(final byte[] b) {
    return getUnsignedLong(b, 0);
  }

  /**
   * Reads a little-endian 8-byte unsigned long from an offset in the given array.
   * @param b The array to read from.
   * @param offset The offset in the array to start reading from.
   * @return A long integer.
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static BigInteger getUnsignedLong(final byte[] b, final int offset) {
    long l = getLong(b, offset);
    BigInteger bi = new BigInteger(l+"");
    if (bi.compareTo(BigInteger.ZERO) < 0) {
      bi = bi.add(TWO_COMPL_REF);
    }
    return bi;
  }

  /**
   * Reads a little-endian 8-byte long from the beginning of the given array.
   * @param b The array to read from.
   * @return A long integer.
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static long getLong(final byte[] b) {
    return getLong(b, 0);
  }

  /**
   * Reads a little-endian 8-byte long from an offset in the given array.
   * @param b The array to read from.
   * @param offset The offset in the array to start reading from.
   * @return A long integer.
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static long getLong(final byte[] b, final int offset) {
    return (b[offset + 0] & 0xFFL) << 0
        | (b[offset + 1] & 0xFFL) << 8
        | (b[offset + 2] & 0xFFL) << 16
        | (b[offset + 3] & 0xFFL) << 24
        | (b[offset + 4] & 0xFFL) << 32
        | (b[offset + 5] & 0xFFL) << 40
        | (b[offset + 6] & 0xFFL) << 48
        | (b[offset + 7] & 0xFFL) << 56;
  }

  /**
   * Writes a little-endian 8-byte long at the beginning of the given array.
   * @param b The array to write to.
   * @param n A long integer.
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static void setLong(final byte[] b, final long n) {
    setLong(b, n, 0);
  }

  /**
   * Writes a little-endian 8-byte long at an offset in the given array.
   * @param b The array to write to.
   * @param n A long integer.
   * @param offset The offset in the array to start writing at.
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static void setLong(final byte[] b, final long n, final int offset) {
    b[offset + 0] = (byte) (n >>> 0);
    b[offset + 1] = (byte) (n >>> 8);
    b[offset + 2] = (byte) (n >>> 16);
    b[offset + 3] = (byte) (n >>> 24);
    b[offset + 4] = (byte) (n >>> 32);
    b[offset + 5] = (byte) (n >>> 40);
    b[offset + 6] = (byte) (n >>>  48);
    b[offset + 7] = (byte) (n >>>  56);
  }

  /**
   * Writes a little-endian 8-byte unsigned long at the beginning of the given array.
   * @param b The array to write to.
   * @param n An unsigned long integer.
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static void setUnsignedLong(final byte[] b, final BigInteger n) {
    setUnsignedLong(b, n, 0);
  }

  /**
   * Writes a little-endian 8-byte unsigned long at an offset in the given array.
   * @param b The array to write to.
   * @param offset The offset in the array to start writing at.
   * @param n An unsigned long integer.
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static void setUnsignedLong(final byte[] b, final BigInteger n, final int offset) {
    setLong(b, n.longValue(), offset);
  }

  /**
   * Creates a new byte array containing a little-endian 8-byte long integer.
   * @param n A long integer.
   * @return A new byte array containing the given value.
   */
  public static byte[] fromLong(final long n) {
    final byte[] b = new byte[8];
    setLong(b, n);
    return b;
  }

  /**
   * Creates a new byte array containing a little-endian 8-byte unsigned long integer.
   * @param n An unsigned long integer.
   * @return A new byte array containing the given value.
   */
  public static byte[] fromUnsignedLong(final BigInteger n) {
    final byte[] b = new byte[8];
    setUnsignedLong(b, n);
    return b;
  }

  /**
   * Reads a little-endian 4-byte float from the beginning of the given array.
   * @param b The array to read from.
   * @return a float
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static float getFloat(final byte[] b) {
    return getFloat(b, 0);
  }

  /**
   * Reads a little-endian 4-byte float from an offset in the given array.
   * @param b The array to read from.
   * @param offset The offset in the array to start reading from.
   * @return a float
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static float getFloat(final byte[] b, final int offset) {
    return Float.intBitsToFloat(getInt(b, offset));
  }

  /**
   * Writes a little-endian 4-byte float at the beginning of the given array.
   * @param b The array to write to.
   * @param n a float
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static void setFloat(final byte[] b, final float n) {
    setFloat(b, n, 0);
  }

  /**
   * Writes a little-endian 4-byte float at an offset in the given array.
   * @param b The array to write to.
   * @param offset The offset in the array to start writing at.
   * @param n a float
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static void setFloat(final byte[] b, final float n, final int offset) {
    setInt(b, Float.floatToIntBits(n), offset);
  }

  /**
   * Creates a new byte array containing a little-endian 4-byte float.
   * @param n A float
   * @return A new byte array containing the given value.
   */
  public static byte[] fromFloat(float n) {
    byte[] b = new byte[4];
    setFloat(b, n);
    return b;
  }

  /**
   * Reads a little-endian 8-byte double from the beginning of the given array.
   * @param b The array to read from.
   * @return a double
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static double getDouble(final byte[] b) {
    return getDouble(b, 0);
  }

  /**
   * Reads a little-endian 8-byte double from an offset in the given array.
   * @param b The array to read from.
   * @param offset The offset in the array to start reading from.
   * @return a double
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static double getDouble(final byte[] b, final int offset) {
    return Double.longBitsToDouble(getLong(b, offset));
  }

  /**
   * Writes a little-endian 8-byte double at the beginning of the given array.
   * @param b The array to write to.
   * @param n a double
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static void setDouble(final byte[] b, final double n) {
    setDouble(b, n, 0);
  }

  /**
   * Writes a little-endian 8-byte double at an offset in the given array.
   * @param b The array to write to.
   * @param offset The offset in the array to start writing at.
   * @param n a double
   * @throws IndexOutOfBoundsException if the byte array is too small.
   */
  public static void setDouble(final byte[] b, final double n, final int offset) {
    setLong(b, Double.doubleToLongBits(n), offset);
  }

  /**
   * Creates a new byte array containing a little-endian 8-byte double.
   * @param n A double
   * @return A new byte array containing the given value.
   */
  public static byte[] fromDouble(double n) {
    byte[] b = new byte[8];
    setDouble(b, n);
    return b;
  }

  /** Transforms a string into an UTF-8 encoded byte array.  */
  public static byte[] UTF8(final String s) {
    return s.getBytes(CharsetUtil.UTF_8);
  }

  /** Transforms a string into an ISO-8859-1 encoded byte array.  */
  public static byte[] ISO88591(final String s) {
    return s.getBytes(CharsetUtil.ISO_8859_1);
  }

  // ---------------------------- //
  // Pretty-printing byte arrays. //
  // ---------------------------- //

  private static final byte[] HEX = {
      '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
      'A', 'B', 'C', 'D', 'E', 'F'
  };

  /**
   * Pretty-prints a byte array into a human-readable output buffer.
   * @param outbuf The buffer where to write the output.
   * @param array The (possibly {@code null}) array to pretty-print.
   */
  public static void pretty(final StringBuilder outbuf, final byte[] array) {
    if (array == null) {
      outbuf.append("null");
      return;
    }
    int ascii = 0;
    final int start_length = outbuf.length();
    final int n = array.length;
    outbuf.ensureCapacity(start_length + 1 + n + 1);
    outbuf.append('"');
    for (int i = 0; i < n; i++) {
      final byte b = array[i];
      if (' ' <= b && b <= '~') {
        ascii++;
        outbuf.append((char) b);
      } else if (b == '\n') {
        outbuf.append('\\').append('n');
      } else if (b == '\t') {
        outbuf.append('\\').append('t');
      } else {
        outbuf.append("\\x")
            .append((char) HEX[(b >>> 4) & 0x0F])
            .append((char) HEX[b & 0x0F]);
      }
    }
    if (ascii < n / 2) {
      outbuf.setLength(start_length);
      outbuf.append(Arrays.toString(array));
    } else {
      outbuf.append('"');
    }
  }

  /**
   * Pretty-prints an array of byte arrays into a human-readable output buffer.
   * @param outbuf The buffer where to write the output.
   * @param arrays The (possibly {@code null}) array of arrays to pretty-print.
   * @since 1.3
   */
  public static void pretty(final StringBuilder outbuf, final byte[][] arrays) {
    if (arrays == null) {
      outbuf.append("null");
      return;
    } else {  // Do some right-sizing.
      int size = 2;
      for (int i = 0; i < arrays.length; i++) {
        size += 2 + 2 + arrays[i].length;
      }
      outbuf.ensureCapacity(outbuf.length() + size);
    }
    outbuf.append('[');
    for (int i = 0; i < arrays.length; i++) {
      Bytes.pretty(outbuf, arrays[i]);
      outbuf.append(", ");
    }
    outbuf.setLength(outbuf.length() - 2);  // Remove the last ", "
    outbuf.append(']');
  }

  /**
   * Pretty-prints a byte array into a human-readable string.
   * @param array The (possibly {@code null}) array to pretty-print.
   * @return The array in a pretty-printed string.
   */
  public static String pretty(final byte[] array) {
    if (array == null) {
      return "null";
    }
    final StringBuilder buf = new StringBuilder(1 + array.length + 1);
    pretty(buf, array);
    return buf.toString();
  }

  // This doesn't really belong here but it doesn't belong anywhere else
  // either, so let's put it close to the other pretty-printing functions.
  /**
   * Pretty-prints a {@code long} into a fixed-width hexadecimal number.
   * @return A string of the form {@code 0x0123456789ABCDEF}.
   */
  public static String hex(long v) {
    final byte[] buf = new byte[2 + 16];
    buf[0] = '0';
    buf[1] = 'x';
    int i = 2 + 16;
    do {
      buf[--i] = HEX[(int) v & 0x0F];
      v >>>= 4;
    } while (v != 0);
    for (/**/; i > 1; i--) {
      buf[i] = '0';
    }
    return new String(buf);
  }

  /**
   * Pretty-prints all the bytes of a buffer into a human-readable string.
   * @param buf The (possibly {@code null}) buffer to pretty-print.
   * @return The buffer in a pretty-printed string.
   */
  public static String pretty(final ByteBuf buf) {
    if (buf == null) {
      return "null";
    }
    byte[] array = new byte[buf.readableBytes()];
    buf.getBytes(buf.readerIndex(), array);
    return pretty(array);
  }

  // ---------------------- //
  // Comparing byte arrays. //
  // ---------------------- //
  // Don't ask me why this isn't in java.util.Arrays.

  /**
   * A singleton {@link Comparator} for non-{@code null} byte arrays.
   * @see #memcmp
   */
  public static final MemCmp MEMCMP = new MemCmp();

  /** {@link Comparator} for non-{@code null} byte arrays.  */
  private static final class MemCmp implements Comparator<byte[]> {

    private MemCmp() {  // Can't instantiate outside of this class.
    }

    @Override
    public int compare(final byte[] a, final byte[] b) {
      return memcmp(a, b);
    }

  }

  /**
   * {@code memcmp} in Java, hooray.
   * @param a First non-{@code null} byte array to compare.
   * @param b Second non-{@code null} byte array to compare.
   * @return 0 if the two arrays are identical, otherwise the difference
   * between the first two different bytes, otherwise the different between
   * their lengths.
   */
  public static int memcmp(final byte[] a, final byte[] b) {
    final int length = Math.min(a.length, b.length);
    if (a == b) {  // Do this after accessing a.length and b.length
      return 0;    // in order to NPE if either a or b is null.
    }
    for (int i = 0; i < length; i++) {
      if (a[i] != b[i]) {
        return (a[i] & 0xFF) - (b[i] & 0xFF);  // "promote" to unsigned.
      }
    }
    return a.length - b.length;
  }

  /**
   * {@code memcmp(3)} with a given offset and length.
   * @param a First non-{@code null} byte array to compare.
   * @param b Second non-{@code null} byte array to compare.
   * @param offset The offset at which to start comparing both arrays.
   * @param length The number of bytes to compare.
   * @return 0 if the two arrays are identical, otherwise the difference
   * between the first two different bytes (treated as unsigned), otherwise
   * the different between their lengths.
   * @throws IndexOutOfBoundsException if either array isn't large enough.
   */
  public static int memcmp(final byte[] a, final byte[] b,
                           final int offset, int length) {
    if (a == b && a != null) {
      return 0;
    }
    length += offset;
    for (int i = offset; i < length; i++) {
      if (a[i] != b[i]) {
        return (a[i] & 0xFF) - (b[i] & 0xFF);  // "promote" to unsigned.
      }
    }
    return 0;
  }

  /**
   * De-duplicates two byte arrays.
   * <p>
   * If two byte arrays have the same contents but are different, this
   * function helps to re-use the old one and discard the new copy.
   * @param old The existing byte array.
   * @param neww The new byte array we're trying to de-duplicate.
   * @return {@code old} if {@code neww} is a different array with the same
   * contents, otherwise {@code neww}.
   */
  public static byte[] deDup(final byte[] old, final byte[] neww) {
    return memcmp(old, neww) == 0 ? old : neww;
  }

  /**
   * Tests whether two byte arrays have the same contents.
   * @param a First non-{@code null} byte array to compare.
   * @param b Second non-{@code null} byte array to compare.
   * @return {@code true} if the two arrays are identical,
   * {@code false} otherwise.
   */
  public static boolean equals(final byte[] a, final byte[] b) {
    return memcmp(a, b) == 0;
  }

  /**
   * {@code memcmp(3)} in Java for possibly {@code null} arrays, hooray.
   * @param a First possibly {@code null} byte array to compare.
   * @param b Second possibly {@code null} byte array to compare.
   * @return 0 if the two arrays are identical (or both are {@code null}),
   * otherwise the difference between the first two different bytes (treated
   * as unsigned), otherwise the different between their lengths (a {@code
   * null} byte array is considered shorter than an empty byte array).
   */
  public static int memcmpMaybeNull(final byte[] a, final byte[] b) {
    if (a == null) {
      if (b == null) {
        return 0;
      }
      return -1;
    } else if (b == null) {
      return 1;
    }
    return memcmp(a, b);
  }

  public static int getBitSetSize(int items) {
    return (items + 7) / 8;
  }

  public static byte[] fromBitSet(BitSet bits, int colCount) {
    byte[] bytes = new byte[getBitSetSize(colCount)];
    for (int i = 0; i < bits.length(); i++) {
      if (bits.get(i)) {
        bytes[i / 8] |= 1 << (i % 8);
      }
    }
    return bytes;
  }

  public static BitSet toBitSet(byte[] b, int offset, int colCount) {
    BitSet bs = new BitSet(colCount);
    for (int i = 0; i < colCount; i++) {
      if ((b[offset + (i / 8)] >> (i % 8) & 1) == 1) {
        bs.set(i);
      }
    }
    return bs;
  }

  /**
   * This method will apply xor on the left most bit of the provided byte. This is used in YB to
   * have unsigned data types sorting correctly.
   * @param value byte whose left most bit will be xor'd
   * @return same byte with xor applied on the left most bit
   */
  public static byte xorLeftMostBit(byte value) {
    value ^= (1 << 7);
    return value;
  }

  /**
   * Get the byte array representation of this string, with UTF8 encoding
   * @param data String get the byte array from
   * @return UTF8 byte array
   */
  public static byte[] fromString(String data) {
    return UTF8(data);
  }

  /**
   * Get a string from the passed byte array, with UTF8 encoding
   * @param b byte array to convert to string, possibly coming from {@link #fromString(String)}
   * @return A new string built with the byte array
   */
  public static String getString(byte[] b) {
    return getString(b, 0, b.length);
  }

  public static String getString(Slice slice) {
    return slice.toString(CharsetUtil.UTF_8);
  }

  /**
   * Get a string from the passed byte array, at the specified offset and for the specified
   * length, with UTF8 encoding
   * @param b byte array to convert to string, possibly coming from {@link #fromString(String)}
   * @param offset where to start reading from in the byte array
   * @param len how many bytes we should read
   * @return A new string built with the byte array
   */
  public static String getString(byte[] b, int offset, int len) {
    if (len == 0) {
      return "";
    }
    return new String(b, offset, len, CharsetUtil.UTF_8);
  }

  /**
   * Utility methd to write a byte array to a data output. Equivalent of doing a writeInt of the
   * length followed by a write of the byte array. Convert back with {@link #readByteArray}
   * @param dataOutput
   * @param b
   * @throws IOException
   */
  public static void writeByteArray(DataOutput dataOutput, byte[] b) throws IOException {
    dataOutput.writeInt(b.length);
    dataOutput.write(b);
  }

  /**
   * Utility method to read a byte array written the way {@link #writeByteArray} does it.
   * @param dataInput
   * @return
   * @throws IOException
   */
  public static byte[] readByteArray(DataInput dataInput) throws IOException {
    int len = dataInput.readInt();
    byte[] data = new byte[len];
    dataInput.readFully(data);
    return data;
  }

  /** A convenient map keyed with a byte array.  */
  public static final class ByteMap<V> extends TreeMap<byte[], V>
      implements Iterable<Map.Entry<byte[], V>> {

    public ByteMap() {
      super(MEMCMP);
    }

    /** Returns an iterator that goes through all the entries in this map.  */
    @Override
    public Iterator<Map.Entry<byte[], V>> iterator() {
      return super.entrySet().iterator();
    }

    /** {@code byte[]} friendly implementation.  */
    @Override
    public String toString() {
      final int size = size();
      if (size == 0) {
        return "{}";
      }
      final StringBuilder buf = new StringBuilder(size << 4);
      buf.append('{');
      for (final Map.Entry<byte[], V> e : this) {
        Bytes.pretty(buf, e.getKey());
        buf.append('=');
        final V value = e.getValue();
        if (value instanceof byte[]) {
          Bytes.pretty(buf, (byte[]) value);
        } else {
          buf.append(value == this ? "(this map)" : value);
        }
        buf.append(", ");
      }
      buf.setLength(buf.length() - 2);  // Remove the extra ", ".
      buf.append('}');
      return buf.toString();
    }

    private static final long serialVersionUID = 1280744742;

  }

}
