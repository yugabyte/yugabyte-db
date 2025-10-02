/**
 * Copyright (C) 2011 the original author or authors.
 *
 * See the LICENSE.txt file distributed with this work for additional
 * information regarding copyright ownership.
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 *
 * The following only applies to changes made to this file as part of YugabyteDB development.
 *
 * Portions Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 *
 */
package org.yb.util;

import com.google.common.base.Preconditions;
import org.yb.annotations.InterfaceAudience;

import java.nio.Buffer;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.CharBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.Charset;
import java.nio.charset.CharsetDecoder;
import java.nio.charset.CharsetEncoder;
import java.nio.charset.CoderResult;
import java.nio.charset.CodingErrorAction;
import java.util.IdentityHashMap;
import java.util.Map;

@InterfaceAudience.Private
public final class Slices
{
  /**
   * A buffer whose capacity is {@code 0}.
   */
  public static final Slice EMPTY_SLICE = new Slice(0);

  private Slices()
  {
  }

  public static Slice ensureSize(Slice existingSlice, int minWritableBytes)
  {
    if (existingSlice == null) {
      existingSlice = EMPTY_SLICE;
    }

    if (minWritableBytes <= existingSlice.length()) {
      return existingSlice;
    }

    int newCapacity;
    if (existingSlice.length() == 0) {
      newCapacity = 1;
    }
    else {
      newCapacity = existingSlice.length();
    }
    int minNewCapacity = existingSlice.length() + minWritableBytes;
    while (newCapacity < minNewCapacity) {
      newCapacity <<= 1;
    }

    Slice newSlice = Slices.allocate(newCapacity);
    newSlice.setBytes(0, existingSlice, 0, existingSlice.length());
    return newSlice;
  }

  public static Slice allocate(int capacity)
  {
    if (capacity == 0) {
      return EMPTY_SLICE;
    }
    return new Slice(capacity);
  }

  public static Slice wrappedBuffer(byte[] array)
  {
    if (array.length == 0) {
      return EMPTY_SLICE;
    }
    return new Slice(array);
  }

  public static Slice copiedBuffer(ByteBuffer source, int sourceOffset, int length)
  {
    Preconditions.checkNotNull(source, "source is null");
    int newPosition = source.position() + sourceOffset;
    return copiedBuffer((ByteBuffer) source.duplicate().order(ByteOrder.LITTLE_ENDIAN).clear().limit(newPosition + length).position(newPosition));
  }

  public static Slice copiedBuffer(ByteBuffer source)
  {
    Preconditions.checkNotNull(source, "source is null");
    Slice copy = allocate(source.limit() - source.position());
    copy.setBytes(0, source.duplicate().order(ByteOrder.LITTLE_ENDIAN));
    return copy;
  }

  public static Slice copiedBuffer(String string, Charset charset)
  {
    Preconditions.checkNotNull(string, "string is null");
    Preconditions.checkNotNull(charset, "charset is null");

    return wrappedBuffer(string.getBytes(charset));
  }

  public static ByteBuffer encodeString(CharBuffer src, Charset charset)
  {
    final CharsetEncoder encoder = getEncoder(charset);
    final ByteBuffer dst = ByteBuffer.allocate(
        (int) ((double) src.remaining() * encoder.maxBytesPerChar()));
    try {
      CoderResult cr = encoder.encode(src, dst, true);
      if (!cr.isUnderflow()) {
        cr.throwException();
      }
      cr = encoder.flush(dst);
      if (!cr.isUnderflow()) {
        cr.throwException();
      }
    }
    catch (CharacterCodingException x) {
      throw new IllegalStateException(x);
    }
    // See https://github.com/yugabyte/yugabyte-db/issues/6712
    ((Buffer)dst).flip();
    return dst;
  }

  public static String decodeString(ByteBuffer src, Charset charset)
  {
    final CharsetDecoder decoder = getDecoder(charset);
    final CharBuffer dst = CharBuffer.allocate(
        (int) ((double) src.remaining() * decoder.maxCharsPerByte()));
    try {
      CoderResult cr = decoder.decode(src, dst, true);
      if (!cr.isUnderflow()) {
        cr.throwException();
      }
      cr = decoder.flush(dst);
      if (!cr.isUnderflow()) {
        cr.throwException();
      }
    }
    catch (CharacterCodingException x) {
      throw new IllegalStateException(x);
    }
    // See https://github.com/yugabyte/yugabyte-db/issues/6712
    return ((Buffer)dst).flip().toString();
  }

  /**
   * Toggles the endianness of the specified 16-bit short integer.
   */
  public static short swapShort(short value)
  {
    return (short) (value << 8 | value >>> 8 & 0xff);
  }

  /**
   * Toggles the endianness of the specified 32-bit integer.
   */
  public static int swapInt(int value)
  {
    return swapShort((short) value) << 16 |
        swapShort((short) (value >>> 16)) & 0xffff;
  }

  /**
   * Toggles the endianness of the specified 64-bit long integer.
   */
  public static long swapLong(long value)
  {
    return (long) swapInt((int) value) << 32 |
        swapInt((int) (value >>> 32)) & 0xffffffffL;
  }

  private static final ThreadLocal<Map<Charset, CharsetEncoder>> encoders =
      new ThreadLocal<Map<Charset, CharsetEncoder>>()
      {
        @Override
        protected Map<Charset, CharsetEncoder> initialValue()
        {
          return new IdentityHashMap<Charset, CharsetEncoder>();
        }
      };

  private static final ThreadLocal<Map<Charset, CharsetDecoder>> decoders =
      new ThreadLocal<Map<Charset, CharsetDecoder>>()
      {
        @Override
        protected Map<Charset, CharsetDecoder> initialValue()
        {
          return new IdentityHashMap<Charset, CharsetDecoder>();
        }
      };

  /**
   * Returns a cached thread-local {@link CharsetEncoder} for the specified
   * <tt>charset</tt>.
   */
  private static CharsetEncoder getEncoder(Charset charset)
  {
    if (charset == null) {
      throw new NullPointerException("charset");
    }

    Map<Charset, CharsetEncoder> map = encoders.get();
    CharsetEncoder e = map.get(charset);
    if (e != null) {
      e.reset();
      e.onMalformedInput(CodingErrorAction.REPLACE);
      e.onUnmappableCharacter(CodingErrorAction.REPLACE);
      return e;
    }

    e = charset.newEncoder();
    e.onMalformedInput(CodingErrorAction.REPLACE);
    e.onUnmappableCharacter(CodingErrorAction.REPLACE);
    map.put(charset, e);
    return e;
  }


  /**
   * Returns a cached thread-local {@link CharsetDecoder} for the specified
   * <tt>charset</tt>.
   */
  private static CharsetDecoder getDecoder(Charset charset)
  {
    if (charset == null) {
      throw new NullPointerException("charset");
    }

    Map<Charset, CharsetDecoder> map = decoders.get();
    CharsetDecoder d = map.get(charset);
    if (d != null) {
      d.reset();
      d.onMalformedInput(CodingErrorAction.REPLACE);
      d.onUnmappableCharacter(CodingErrorAction.REPLACE);
      return d;
    }

    d = charset.newDecoder();
    d.onMalformedInput(CodingErrorAction.REPLACE);
    d.onUnmappableCharacter(CodingErrorAction.REPLACE);
    map.put(charset, d);
    return d;
  }

}
