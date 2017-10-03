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
package org.kududb.client;

import org.kududb.annotations.InterfaceAudience;
import org.kududb.annotations.InterfaceStability;

/**
 * The parent class of all {@link RuntimeException} created by this package.
 */
@InterfaceAudience.Public
@InterfaceStability.Evolving
@SuppressWarnings("serial")
public abstract class KuduException extends RuntimeException {

  /**
   * Constructor.
   * @param msg The message of the exception, potentially including a stack
   * trace.
   */
  KuduException(final String msg) {
    super(msg);
  }

  /**
   * Constructor.
   * @param msg The message of the exception, potentially including a stack
   * trace.
   * @param cause The exception that caused this one to be thrown.
   */
  KuduException(final String msg, final Throwable cause) {
    super(msg, cause);
  }

  /**
   * Factory method to make it possible to create an exception from another
   * one without having to resort to reflection, which is annoying to use.
   * Sub-classes that want to provide this internal functionality should
   * implement this method.
   * @param arg Some arbitrary parameter to help build the new instance.
   * @param rpc The RPC that failed, if any.  Can be {@code null}.
   */
  KuduException make(final Object arg, final KuduRpc<?> rpc) {
    throw new AssertionError("Must not be used.");
  }
}
