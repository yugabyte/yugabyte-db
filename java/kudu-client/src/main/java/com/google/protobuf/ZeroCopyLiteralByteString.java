/*
 * Copyright (C) 2013  The Async HBase Authors.  All rights reserved.
 * This file is part of Async HBase.
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
package com.google.protobuf;  // This is a lie.

/**
 * Helper class to extract byte arrays from {@link ByteString} without copy.
 * <p>
 * Without this protobufs would force us to copy every single byte array out
 * of the objects de-serialized from the wire (which already do one copy, on
 * top of the copies the JVM does to go from kernel buffer to C buffer and
 * from C buffer to JVM buffer).
 * <p>
 * <strong>This class isn't part of the public API.</strong>
 */
public final class ZeroCopyLiteralByteString extends LiteralByteString {

  /** Private constructor so this class cannot be instantiated. */
  private ZeroCopyLiteralByteString() {
    super(null);
    throw new UnsupportedOperationException("Should never be here.");
  }

  /**
   * Wraps a byte array in a {@link ByteString} without copying it.
   */
  public static ByteString wrap(final byte[] array) {
    return new LiteralByteString(array);
  }

  /**
   * Extracts the byte array from the given {@link ByteString} without copy.
   * @param buf A buffer from which to extract the array.  This buffer must be
   * actually an instance of a {@code LiteralByteString}.
   */
  public static byte[] zeroCopyGetBytes(final ByteString buf) {
    if (buf instanceof LiteralByteString) {
      return ((LiteralByteString) buf).bytes;
    }
    throw new UnsupportedOperationException("Need a LiteralByteString, got a "
        + buf.getClass().getName());
  }

}