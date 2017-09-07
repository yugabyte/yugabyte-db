// Copyright (c) YugaByte, Inc.
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
package com.yugabyte.cql;

/**
 * A hash function that returns a 64-bit hash value.
 *
 * Based on Bob Jenkins' new hash function (http://burtleburtle.net/bob/hash/evahash.html)
 */
public class Jenkins {

  /**
   * Returns a 64-bit hash value of a byte sequence.
   *
   * @param k        the byte sequence
   * @param initval  the initial (seed) value
   * @return         the 64-bit hash value
   */
  public static long hash64(byte[] k, long initval) {
    // Set up the internal state
    long a = 0xe08c1d668b756f82L; // the golden ratio; an arbitrary value
    long b = 0xe08c1d668b756f82L;
    long c = initval;             // variable initialization of internal state

    int pos = 0;

    // handle most of the key
    while (k.length - pos >= 24) {
      a += getLong(k, pos); pos += 8;
      b += getLong(k, pos); pos += 8;
      c += getLong(k, pos); pos += 8;

      // mix64(a, b, c);
      a -= b; a -= c; a ^= (c >>> 43);
      b -= c; b -= a; b ^= (a << 9);
      c -= a; c -= b; c ^= (b >>> 8);
      a -= b; a -= c; a ^= (c >>> 38);
      b -= c; b -= a; b ^= (a << 23);
      c -= a; c -= b; c ^= (b >>> 5);
      a -= b; a -= c; a ^= (c >>> 35);
      b -= c; b -= a; b ^= (a << 49);
      c -= a; c -= b; c ^= (b >>> 11);
      a -= b; a -= c; a ^= (c >>> 12);
      b -= c; b -= a; b ^= (a << 18);
      c -= a; c -= b; c ^= (b >>> 22);
    }

    // handle the last 23 bytes
    c += k.length;
    switch (k.length - pos) { // all the case statements fall through
      case 23: c += getByte(k, pos + 22) << 56;
      case 22: c += getByte(k, pos + 21) << 48;
      case 21: c += getByte(k, pos + 20) << 40;
      case 20: c += getByte(k, pos + 19) << 32;
      case 19: c += getByte(k, pos + 18) << 24;
      case 18: c += getByte(k, pos + 17) << 16;
      case 17: c += getByte(k, pos + 16) <<  8;
      // the first byte of c is reserved for the length
      case 16: b += getLong(k, pos + 8); a += getLong(k, pos); break; // special handling
      case 15: b += getByte(k, pos + 14) << 48;
      case 14: b += getByte(k, pos + 13) << 40;
      case 13: b += getByte(k, pos + 12) << 32;
      case 12: b += getByte(k, pos + 11) << 24;
      case 11: b += getByte(k, pos + 10) << 16;
      case 10: b += getByte(k, pos +  9) <<  8;
      case  9: b += getByte(k, pos +  8);
      case  8: a += getLong(k, pos); break; // special handling
      case  7: a += getByte(k, pos +  6) << 48;
      case  6: a += getByte(k, pos +  5) << 40;
      case  5: a += getByte(k, pos +  4) << 32;
      case  4: a += getByte(k, pos +  3) << 24;
      case  3: a += getByte(k, pos +  2) << 16;
      case  2: a += getByte(k, pos +  1) <<  8;
      case  1: a += getByte(k, pos);
      // case 0: nothing left to add
    }

    // mix64(a, b, c);
    a -= b; a -= c; a ^= (c >>> 43);
    b -= c; b -= a; b ^= (a << 9);
    c -= a; c -= b; c ^= (b >>> 8);
    a -= b; a -= c; a ^= (c >>> 38);
    b -= c; b -= a; b ^= (a << 23);
    c -= a; c -= b; c ^= (b >>> 5);
    a -= b; a -= c; a ^= (c >>> 35);
    b -= c; b -= a; b ^= (a << 49);
    c -= a; c -= b; c ^= (b >>> 11);
    a -= b; a -= c; a ^= (c >>> 12);
    b -= c; b -= a; b ^= (a << 18);
    c -= a; c -= b; c ^= (b >>> 22);

    return c;
  }

  /**
   * Retrieves a byte in a byte array as a long.
   *
   * @param k    the byte array
   * @param pos  the byte position
   * @return     the byte as long
   */
  private static long getByte(byte[] k, int pos) {
    return (long)k[pos] & (long)0xff;
  }

  /**
   * Retrieves a long in a byte array in little-endian order.
   *
   * @param k    the byte array
   * @param pos  the long position
   * @return     the long in little-endian order
   */
  private static long getLong(byte[] k, int pos) {
    return (getByte(k, pos)           |
            getByte(k, pos + 1) <<  8 |
            getByte(k, pos + 2) << 16 |
            getByte(k, pos + 3) << 24 |
            getByte(k, pos + 4) << 32 |
            getByte(k, pos + 5) << 40 |
            getByte(k, pos + 6) << 48 |
            getByte(k, pos + 7) << 56);
  }
}
