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

import com.stumbleupon.async.Deferred;
import org.kududb.annotations.InterfaceAudience;
import org.kududb.annotations.InterfaceStability;

/**
 * This exception notifies the application to throttle its use of Kudu.
 * <p>
 * Since all APIs of {@link AsyncKuduSession} are asynchronous and non-blocking,
 * it's possible that the application would produce RPCs at a rate higher
 * than Kudu is able to handle.  When this happens, {@link AsyncKuduSession}
 * will typically do some buffering up to a certain point beyond which RPCs
 * will fail-fast with this exception, to prevent the application from
 * running itself out of memory.
 * <p>
 * This exception is expected to be handled by having the application
 * throttle or pause itself for a short period of time before retrying the
 * RPC that failed with this exception as well as before sending other RPCs.
 * The reason this exception inherits from {@link NonRecoverableException}
 * instead of {@link RecoverableException} is that the usual course of action
 * when handling a {@link RecoverableException} is to retry right away, which
 * would defeat the whole purpose of this exception.  Here, we want the
 * application to <b>retry after a reasonable delay</b> as well as <b>throttle
 * the pace of creation of new RPCs</b>.  What constitutes a "reasonable
 * delay" depends on the nature of RPCs and rate at which they're produced.
 * <p>
 * One effective strategy to handle this exception is to set a flag to true
 * when this exception is first emitted that causes the application to pause
 * or throttle its use of Kudu.  Then you can retry the RPC that failed
 * (which is accessible through {@link #getFailedRpc}) and add a callback to
 * it in order to unset the flag once the RPC completes successfully.
 * Note that low-throughput applications will typically rarely (if ever)
 * hit this exception, so they don't need complex throttling logic.
 */
@InterfaceAudience.Public
@InterfaceStability.Evolving
@SuppressWarnings("serial")
public final class PleaseThrottleException extends NonRecoverableException
    implements HasFailedRpcException {

  /** The RPC that was failed with this exception.  */
  private final Operation rpc;

  /** A deferred one can wait on before retrying the failed RPC.  */
  private final Deferred deferred;

  /**
   * Constructor.
   * @param msg A message explaining why the application has to throttle.
   * @param cause The exception that requires the application to throttle
   * itself (can be {@code null}).
   * @param rpc The RPC that was made to fail with this exception.
   * @param deferred A deferred one can wait on before retrying the failed RPC.
   */
  PleaseThrottleException(final String msg,
                          final KuduException cause,
                          final Operation rpc,
                          final Deferred deferred) {
    super(msg, cause);
    this.rpc = rpc;
    this.deferred = deferred;
  }

  /**
   * The RPC that was made to fail with this exception.
   */
  public Operation getFailedRpc() {
    return rpc;
  }

  /**
   * Returns a deferred one can wait on before retrying the failed RPC.
   * @since 1.3
   */
  public Deferred getDeferred() {
    return deferred;
  }

}
