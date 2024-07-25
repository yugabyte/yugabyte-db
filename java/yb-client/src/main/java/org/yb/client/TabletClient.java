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

import com.stumbleupon.async.Deferred;
import io.netty.buffer.ByteBuf;
import io.netty.buffer.Unpooled;
import io.netty.channel.Channel;
import io.netty.channel.ChannelFuture;
import io.netty.channel.ChannelFutureListener;
import io.netty.channel.ChannelHandlerContext;
import io.netty.handler.codec.ReplayingDecoder;
import io.netty.handler.timeout.ReadTimeoutException;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.WireProtocol;
import org.yb.annotations.InterfaceAudience;
import org.yb.cdc.CdcService;
import org.yb.master.MasterTypes;
import org.yb.rpc.RpcHeader;
import org.yb.tserver.TserverTypes;
import org.yb.util.Pair;

import java.net.InetSocketAddress;
import java.net.SocketAddress;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Iterator;
import java.util.List;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Stateful handler that manages a connection to a specific TabletServer.
 * <p>
 * This handler manages the RPC IDs, the serialization and de-serialization of
 * RPC requests and responses, and keeps track of the RPC in flights for which
 * a response is currently awaited, as well as temporarily buffered RPCs that
 * are awaiting to be sent to the network.
 * <p>
 * This class needs careful synchronization. It's a non-sharable handler,
 * meaning there is one instance of it per Netty {@link Channel} and each
 * instance is only used by one Netty IO thread at a time.  At the same time,
 * {@link AsyncYBClient} calls methods of this class from random threads at
 * random times. The bottom line is that any data only used in the Netty IO
 * threads doesn't require synchronization, everything else does.
 * <p>
 * Acquiring the monitor on an object of this class will prevent it from
 * accepting write requests as well as buffering requests if the underlying
 * channel isn't connected.
 */
@InterfaceAudience.Private
public class TabletClient extends ReplayingDecoder<Void> {

  public static final Logger LOG = LoggerFactory.getLogger(TabletClient.class);

  private ArrayList<YRpc<?>> pending_rpcs;

  /** The connection header.  */
  private static final byte[] RPC_HEADER = new byte[] { 'Y', 'B', 1 };
  public static final int CONNECTION_CTX_CALL_ID = -3;

  /**
   * A monotonically increasing counter for RPC IDs.
   * RPCs can be sent out from any thread, so we need an atomic integer.
   * RPC IDs can be arbitrary.  So it's fine if this integer wraps around and
   * becomes negative.  They don't even have to start at 0, but we do it for
   * simplicity and ease of debugging.
   */
  private final AtomicInteger rpcid = new AtomicInteger(-1);

  /**
   * The channel we're connected to.
   * This will be {@code null} while we're not connected to the TabletServer.
   * This attribute is volatile because {@link #shutdown} may access it from a
   * different thread, and because while we connect various user threads will
   * test whether it's {@code null}.  Once we're connected and we know what
   * protocol version the server speaks, we'll set this reference.
   */
  private volatile Channel chan;

  /**
   * Set to {@code true} once we've disconnected from the server.
   * This way, if any thread is still trying to use this client after it's
   * been removed from the caches in the {@link AsyncYBClient}, we will
   * immediately fail / reschedule its requests.
   * <p>
   * Manipulating this value requires synchronizing on `this'.
   */
  private boolean dead = false;

  /**
   * Maps an RPC ID to the in-flight RPC that was given this ID.
   * RPCs can be sent out from any thread, so we need a concurrent map.
   */
  private final ConcurrentHashMap<Integer, YRpc<?>> rpcs_inflight =
      new ConcurrentHashMap<Integer, YRpc<?>>();

  private final AsyncYBClient ybClient;

  private final String uuid;

  private final long socketReadTimeoutMs;

  public TabletClient(AsyncYBClient client, String uuid) {
    this.ybClient = client;
    this.uuid = uuid;
    this.socketReadTimeoutMs = client.getDefaultSocketReadTimeoutMs();
  }

  <R> void sendRpc(YRpc<R> rpc) {
    if (!rpc.deadlineTracker.hasDeadline()) {
      LOG.warn(getPeerUuidLoggingString() + " sending an rpc without a timeout " + rpc);
    }
    if (chan != null) {
      final ByteBuf serialized = encode(rpc);
      if (serialized == null) {  // Error during encoding.
        return;  // Stop here.  RPC has been failed already.
      }

      final Channel chan = this.chan;  // Volatile read.
      if (chan != null) {  // Double check if we disconnected during encode().
        chan.writeAndFlush(serialized);
        return;
      }
    }
    boolean tryagain = false;
    boolean copyOfDead;
    synchronized (this) {
      copyOfDead = this.dead;
      // Check if we got connected while entering this synchronized block.
      if (chan != null) {
        tryagain = true;
      } else if (!copyOfDead) {
        if (pending_rpcs == null) {
          pending_rpcs = new ArrayList<YRpc<?>>();
        }
        pending_rpcs.add(rpc);
      }
    }
    if (copyOfDead) {
      failOrRetryRpc(rpc, new ConnectionResetException(null));
      return;
    } else if (tryagain) {
      // This recursion will not lead to a loop because we only get here if we
      // connected while entering the synchronized block above. So when trying
      // a second time,  we will either succeed to send the RPC if we're still
      // connected, or fail through to the code below if we got disconnected
      // in the mean time.
      sendRpc(rpc);
      return;
    }
  }

  private <R> ByteBuf encode(final YRpc<R> rpc) {
    final int rpcid = this.rpcid.incrementAndGet();
    ByteBuf payload;
    final String service = rpc.serviceName();
    final String method = rpc.method();
    try {
      final RpcHeader.RequestHeader.Builder headerBuilder = RpcHeader.RequestHeader.newBuilder()
          .setCallId(rpcid)
          .setRemoteMethod(
              RpcHeader.RemoteMethodPB.newBuilder().setServiceName(service).setMethodName(method));

      // If any timeout is set, find the lowest non-zero one, since this will be the deadline that
      // the server must respect.
      if (rpc.deadlineTracker.hasDeadline() || socketReadTimeoutMs > 0) {
        long millisBeforeDeadline = Long.MAX_VALUE;
        if (rpc.deadlineTracker.hasDeadline()) {
          millisBeforeDeadline = rpc.deadlineTracker.getMillisBeforeDeadline();
        }

        long localRpcTimeoutMs = Long.MAX_VALUE;
        if (socketReadTimeoutMs > 0) {
          localRpcTimeoutMs = socketReadTimeoutMs;
        }

        headerBuilder.setTimeoutMillis((int) Math.min(millisBeforeDeadline, localRpcTimeoutMs));
      }

      payload = rpc.serialize(headerBuilder.build());
    } catch (Exception e) {
        LOG.error("Uncaught exception while serializing RPC: " + rpc, e);
        rpc.errback(e);  // Make the RPC fail with the exception.
        return null;
    }
    final YRpc<?> oldrpc = rpcs_inflight.put(rpcid, rpc);
    if (oldrpc != null) {
      final String wtf = getPeerUuidLoggingString() +
          "WTF?  There was already an RPC in flight with"
          + " rpcid=" + rpcid + ": " + oldrpc
          + ".  This happened when sending out: " + rpc;
      LOG.error(wtf);
      // Make it fail. This isn't an expected failure mode.
      oldrpc.errback(new NonRecoverableException(wtf));
    }

    if (LOG.isDebugEnabled()) {
      LOG.debug(getPeerUuidLoggingString() + chan + " Sending RPC #" + rpcid
          + ", payload=" + payload + ' ' + Bytes.pretty(payload));
    }

    return payload;
  }

  public Deferred<Void> shutdown() {
    // First, check whether we have RPCs in flight and cancel them.
    for (Iterator<YRpc<?>> ite = rpcs_inflight.values().iterator(); ite
        .hasNext();) {
      YRpc<?> rpc = ite.next();
      rpc.errback(new ConnectionResetException(null));
      ite.remove();
    }

    // Same for the pending RPCs.
    synchronized (this) {
      if (pending_rpcs != null) {
        for (Iterator<YRpc<?>> ite = pending_rpcs.iterator(); ite.hasNext();) {
          ite.next().errback(new ConnectionResetException(null));
          ite.remove();
        }
      }
    }

    final Channel chancopy = chan;
    if (chancopy == null) {
      LOG.warn("Channel is missing during client shutdown for {}", getPeerUuidLoggingString());
      return Deferred.fromResult(null);
    }
    String hostPort = getHostPort(chan.remoteAddress());
    LOG.debug("Shutting down channel {}", hostPort);
    final Deferred<Void> d = new Deferred<Void>();
    if (chancopy.isActive()) {
      try {
        chancopy.disconnect().sync();   // ... this is going to set it to null.
        // At this point, all in-flight RPCs are going to be failed.
      } catch (InterruptedException e) {
        LOG.warn("{} {} - failed to disconnect - interrupted",
          getPeerUuidLoggingString(), hostPort);
      }
    } else {
      LOG.warn("Channel {} is not active during shutdown", hostPort);
    }
    // It's OK to call close() on a Channel if it's already closed.
    final ChannelFuture future = chancopy.close();
    // Now wrap the ChannelFuture in a Deferred.
    // Opportunistically check if it's already completed successfully.
    if (future.isSuccess()) {
      d.callback(null);
    } else {
      // If we get here, either the future failed (yeah, that sounds weird)
      // or the future hasn't completed yet (heh).
      future.addListener(new ChannelFutureListener() {
        public void operationComplete(final ChannelFuture future) {
          if (future.isSuccess()) {
            d.callback(null);
            return;
          }
          final Throwable t = future.cause();
          if (t instanceof Exception) {
            d.callback(t);
          } else {
            // Wrap the Throwable because Deferred doesn't handle Throwables,
            // it only uses Exception.
            d.callback(new NonRecoverableException("Failed to shutdown: "
                + TabletClient.this, t));
          }
        }
      });
    }
    return d;
  }

  /**
   * The reason we are suppressing the unchecked conversions is because the YRpc is coming
   * from a collection that has RPCs with different generics, and there's no way to get "decoded"
   * casted correctly. The best we can do is to rely on the RPC to decode correctly,
   * and to not pass an Exception in the callback.
   */
  @Override
  @SuppressWarnings("unchecked")
  protected void decode(ChannelHandlerContext ctx, ByteBuf buf,
                        List<Object> list) {
    final long start = System.nanoTime();
    final int rdx = buf.readerIndex();
    LOG.debug("------------------>> ENTERING DECODE >>------------------");

    if (buf == null) {
      return;
    }

    CallResponse response = new CallResponse(buf);
    if (response.isEmpty()) {
      // Skip empty messages which we are using as heartbeats.
      return;
    }

    RpcHeader.ResponseHeader header = response.getHeader();
    if (!header.hasCallId()) {
      final int size = response.getTotalResponseSize();
      final String msg = getPeerUuidLoggingString() + "RPC response (size: " + size + ") doesn't"
          + " have a call ID: " + header + ", buf=" + Bytes.pretty(buf);
      LOG.error(msg);
      throw new NonRecoverableException(msg);
    }
    final int rpcid = header.getCallId();

    @SuppressWarnings("rawtypes")
    final YRpc rpc = rpcs_inflight.remove(rpcid);

    if (rpc == null) {
      final String msg = getPeerUuidLoggingString() + "Invalid rpcid: " + rpcid + " found in "
          + buf + '=' + Bytes.pretty(buf);
      LOG.error(msg);
      // The problem here is that we don't know which Deferred corresponds to
      // this RPC, since we don't have a valid ID.  So we're hopeless, we'll
      // never be able to recover because responses are not framed, we don't
      // know where the next response will start...  We have to give up here
      // and throw this outside of our Netty handler, so Netty will call our
      // exception handler where we'll close this channel, which will cause
      // all RPCs in flight to be failed.
      throw new NonRecoverableException(msg);
    }

    Pair<Object, Object> decoded = null;
    Exception exception = null;
    YBException retryableHeaderException = null;
    if (header.hasIsError() && header.getIsError()) {
      RpcHeader.ErrorStatusPB.Builder errorBuilder = RpcHeader.ErrorStatusPB.newBuilder();
      YRpc.readProtobuf(response.getPBMessage(), errorBuilder);
      RpcHeader.ErrorStatusPB error = errorBuilder.build();
      if (error.getCode().equals(RpcHeader.ErrorStatusPB.RpcErrorCodePB.ERROR_SERVER_TOO_BUSY)) {
        // We can't return right away, we still need to remove ourselves from 'rpcs_inflight', so we
        // populate 'retryableHeaderException'.
        retryableHeaderException = new TabletServerErrorException(uuid, error);
      } else {
        String message = getPeerUuidLoggingString() +
            "Tablet server sent error " + error.getMessage();
        exception = new NonRecoverableException(message);
        LOG.error(message); // can be useful
      }
    } else {
      try {
        decoded = rpc.deserialize(response, this.uuid);
      } catch (Exception ex) {
        exception = ex;
      }
    }
    if (LOG.isDebugEnabled()) {
      LOG.debug(getPeerUuidLoggingString() + "rpcid=" + rpcid
          + ", response size=" + (buf.readerIndex() - rdx) + " bytes"
          + ", " + actualReadableBytes() + " readable bytes left"
          + ", rpc=" + rpc);
    }

    // This check is specifically for the ERROR_SERVER_TOO_BUSY case above.
    if (retryableHeaderException != null) {
      ybClient.handleRetryableError(rpc, retryableHeaderException, this);
      return;
    }

    // We can get this Message from within the RPC's expected type,
    // so convert it into an exception and nullify decoded so that we use the errback route.
    // Have to do it for both TS and Master errors.
    if (decoded != null) {
      if (decoded.getSecond() instanceof TserverTypes.TabletServerErrorPB) {
        TserverTypes.TabletServerErrorPB error =
            (TserverTypes.TabletServerErrorPB) decoded.getSecond();
        exception = dispatchTSErrorOrReturnException(rpc, error);
        if (exception == null) {
          // It was taken care of.
          return;
        } else {
          // We're going to errback.
          decoded = null;
        }
      } else if (decoded.getSecond() instanceof MasterTypes.MasterErrorPB) {
        MasterTypes.MasterErrorPB error = (MasterTypes.MasterErrorPB) decoded.getSecond();
        exception = dispatchMasterErrorOrReturnException(rpc, error);
        if (exception == null) {
          // Exception was taken care of.
          return;
        } else {
          decoded = null;
        }
      }
      else if (decoded.getSecond() instanceof CdcService.CDCErrorPB) {
        CdcService.CDCErrorPB error = (CdcService.CDCErrorPB) decoded.getSecond();
        exception = dispatchCDCErrorOrReturnException(rpc, error);
        if (exception == null) {
          // It was taken care of.
          return;
        } else {
          // We're going to errback.
          decoded = null;
        }
      }
    }

    try {
      if (decoded != null) {
        assert !(decoded.getFirst() instanceof Exception);
        rpc.callback(decoded.getFirst());
      } else {
        rpc.errback(exception);
      }
    } catch (Exception e) {
      LOG.debug(getPeerUuidLoggingString() + "Unexpected exception while handling RPC #" + rpcid
          + ", rpc=" + rpc + ", buf=" + Bytes.pretty(buf), e);
    }
    if (LOG.isDebugEnabled()) {
      LOG.debug("------------------<< LEAVING  DECODE <<------------------"
          + " time elapsed: " + ((System.nanoTime() - start) / 1000) + "us");
    }
    return;  // Stop processing here.  The Deferred does everything else.
  }

  /**
   * Takes care of a few kinds of TS errors that we handle differently, like tablets or leaders
   * moving. Builds and returns an exception if we don't know what to do with it.
   * @param rpc The original RPC call that triggered the error.
   * @param error The error the TS sent.
   * @return An exception if we couldn't dispatch the error, or null.
   */
  private Exception dispatchTSErrorOrReturnException(YRpc rpc,
                                                     TserverTypes.TabletServerErrorPB error) {
    WireProtocol.AppStatusPB.ErrorCode code = error.getStatus().getCode();
    TabletServerErrorException ex = new TabletServerErrorException(uuid, error);
    if (error.getCode() == TserverTypes.TabletServerErrorPB.Code.TABLET_NOT_FOUND) {
      ybClient.handleTabletNotFound(rpc, ex, this);
      // we're not calling rpc.callback() so we rely on the client to retry that RPC
    } else if (code == WireProtocol.AppStatusPB.ErrorCode.SERVICE_UNAVAILABLE ||
               code == WireProtocol.AppStatusPB.ErrorCode.LEADER_NOT_READY_TO_SERVE ||
               error.getCode() ==
                 TserverTypes.TabletServerErrorPB.Code.LEADER_NOT_READY_CHANGE_CONFIG ||
               error.getCode() ==
                 TserverTypes.TabletServerErrorPB.Code.LEADER_NOT_READY_TO_STEP_DOWN ||
               error.getCode() ==
                 TserverTypes.TabletServerErrorPB.Code.LEADER_NOT_READY_TO_SERVE) {
      ybClient.handleRetryableError(rpc, ex, this);
      // The following error codes are an indication that the tablet isn't a leader, or, in case
      // of LEADER_HAS_NO_LEASE, might no longer be the leader due to failing to replicate a leader
      // lease, so we retry looking up the leader anyway.
    } else if (code == WireProtocol.AppStatusPB.ErrorCode.LEADER_HAS_NO_LEASE ||
               code == WireProtocol.AppStatusPB.ErrorCode.ILLEGAL_STATE ||
               code == WireProtocol.AppStatusPB.ErrorCode.ABORTED ||
               error.getCode() == TserverTypes.TabletServerErrorPB.Code.NOT_THE_LEADER) {
      ybClient.handleNotLeader(rpc, ex, this);
    } else {
      return ex;
    }
    return null;
  }

  private Exception dispatchCDCErrorOrReturnException(YRpc rpc,
                                                     CdcService.CDCErrorPB error) {
    WireProtocol.AppStatusPB.ErrorCode code = error.getStatus().getCode();
    CDCErrorException ex = new CDCErrorException(uuid, error);
    if (error.getCode() == CdcService.CDCErrorPB.Code.TABLET_NOT_RUNNING ||
      error.getCode() == CdcService.CDCErrorPB.Code.LEADER_NOT_READY ||
      error.getCode() == CdcService.CDCErrorPB.Code.NOT_LEADER ||
      error.getCode() == CdcService.CDCErrorPB.Code.NOT_RUNNING) {
      // rpc.deadlineTracker.reset();
      ybClient.handleNotLeader(rpc, ex, this);
      // we're not calling rpc.callback() so we rely on the client to retry that RPC
    } else if (code == WireProtocol.AppStatusPB.ErrorCode.SERVICE_UNAVAILABLE ||
      code == WireProtocol.AppStatusPB.ErrorCode.IO_ERROR ||
      code == WireProtocol.AppStatusPB.ErrorCode.LEADER_NOT_READY_TO_SERVE ||
      code == WireProtocol.AppStatusPB.ErrorCode.TIMED_OUT) {
      ybClient.handleRetryableError(rpc, ex, this);
      // The following error codes are an indication that the tablet isn't a leader, or, in case
      // of LEADER_HAS_NO_LEASE, might no longer be the leader due to failing to replicate a leader
      // lease, so we retry looking up the leader anyway.
    }
    else if (error.getCode() == CdcService.CDCErrorPB.Code.TABLET_NOT_FOUND ||
      code == WireProtocol.AppStatusPB.ErrorCode.NOT_FOUND) {
      ybClient.handleTabletNotFound(rpc, ex, this);
    }
    else if (code == WireProtocol.AppStatusPB.ErrorCode.LEADER_HAS_NO_LEASE ||
      code == WireProtocol.AppStatusPB.ErrorCode.ILLEGAL_STATE ||
      code == WireProtocol.AppStatusPB.ErrorCode.ABORTED ||
      error.getCode() == CdcService.CDCErrorPB.Code.NOT_LEADER) {
      ybClient.handleNotLeader(rpc, ex, this);
    } else {
      return ex;
    }
    return null;
  }

  /**
   * Provides different handling for various kinds of master errors: re-uses the
   * mechanisms already in place for handling tablet server errors as much as possible.
   * @param rpc The original RPC call that triggered the error.
   * @param error The error the master sent.
   * @return An exception if we couldn't dispatch the error, or null.
   */
  private Exception dispatchMasterErrorOrReturnException(YRpc rpc,
                                                         MasterTypes.MasterErrorPB error) {
    WireProtocol.AppStatusPB.ErrorCode code = error.getStatus().getCode();
    MasterErrorException ex = new MasterErrorException(uuid, error);
    if (error.getCode() == MasterTypes.MasterErrorPB.Code.NOT_THE_LEADER) {
      ybClient.handleNotLeader(rpc, ex, this);
    } else if (error.getCode() == MasterTypes.MasterErrorPB.Code.CATALOG_MANAGER_NOT_INITIALIZED ||
               error.getCode() == MasterTypes.MasterErrorPB.Code.CAN_RETRY_LOAD_BALANCE_CHECK) {
      ybClient.handleRetryableError(rpc, ex, this);
    } else if (code == WireProtocol.AppStatusPB.ErrorCode.SERVICE_UNAVAILABLE &&
        (!(rpc instanceof GetMasterRegistrationRequest))) {
      // TODO: This is a crutch until we either don't have to retry RPCs going to the
      // same server or use retry policies.
      ybClient.handleRetryableError(rpc, ex, this);
    } else {
      return ex;
    }
    return null;
  }

  /**
   * Decodes the response of an RPC and triggers its {@link Deferred}.
   * <p>
   * This method is used by FrameDecoder when the channel gets
   * disconnected.  The buffer for that channel is passed to this method in
   * case there's anything left in it.
   * @param ctx Unused.
   * @param buf The buffer containing the raw RPC response.
   * @param out output objects list.
   *
   */
  @Override
  protected void decodeLast(final ChannelHandlerContext ctx,
                            final ByteBuf buf,
                            final List<Object> out) {
    // When we disconnect, decodeLast is called instead of decode.
    // We simply check whether there's any data left in the buffer, in which
    // case we attempt to process it.  But if there's no data left, then we
    // don't even bother calling decode() as it'll complain that the buffer
    // doesn't contain enough data, which unnecessarily pollutes the logs.
    if (buf.isReadable()) {
      try {
        decode(ctx, buf, out);
      } finally {
        if (buf.isReadable()) {
          LOG.error(getPeerUuidLoggingString() + "After decoding the last message on " + chan
              + ", there was still some undecoded bytes in the channel's"
              + " buffer (which are going to be lost): "
              + buf + '=' + Bytes.pretty(buf));
        }
      }
    }
  }

  /**
   * Tells whether or not this handler should be used.
   * <p>
   * This method is not synchronized.  You need to synchronize on this
   * instance if you need a memory visibility guarantee.  You may not need
   * this guarantee if you're OK with the RPC finding out that the connection
   * has been reset "the hard way" and you can retry the RPC.  In this case,
   * you can call this method as a hint.  After getting the initial exception
   * back, this thread is guaranteed to see this method return {@code false}
   * without synchronization needed.
   * @return {@code false} if this handler is known to have been disconnected
   * from the server and sending an RPC (via {@link #sendRpc} or any other
   * indirect means such as {@code GetTableLocations()}) will fail immediately
   * by having the RPC's {@link Deferred} called back immediately with a
   * {@link ConnectionResetException}.  This typically means that you got a
   * stale reference (or that the reference to this instance is just about to
   * be invalidated) and that you shouldn't use this instance.
   */
  public boolean isAlive() {
    return !dead;
  }

  /**
   * Ensures that at least a {@code nbytes} are readable from the given buffer.
   * If there aren't enough bytes in the buffer this will raise an exception
   * and cause the {@link ReplayingDecoder} to undo whatever we did thus far
   * so we can wait until we read more from the socket.
   * @param buf Buffer to check.
   * @param nbytes Number of bytes desired.
   */
  static void ensureReadable(final ByteBuf buf, final int nbytes) {
    buf.markReaderIndex();
    buf.skipBytes(nbytes); // can puke with Throwable
    buf.resetReaderIndex();
  }

  @Override
  public void channelActive(final ChannelHandlerContext ctx) {
    final Channel chan = ctx.channel();
    ByteBuf header = connectionHeaderPreamble();
    header.writerIndex(RPC_HEADER.length);
    chan.writeAndFlush(header);
    becomeReady(chan);
  }

  @Override
  public void channelInactive(ChannelHandlerContext ctx) throws Exception {
    super.channelInactive(ctx);
    doCleanup(ctx.channel());
  }

  public void doCleanup(Channel channel) {
    chan = null;
    cleanup(channel);
  }

  /**
   * Cleans up any outstanding or lingering RPC (used when shutting down).
   * <p>
   * All RPCs in flight will fail with a {@link ConnectionResetException} and
   * all edits buffered will be re-scheduled.
   */
  private void cleanup(final Channel chan) {
    final ConnectionResetException exception =
        new ConnectionResetException(getPeerUuidLoggingString() + "Connection reset on " + chan);
    for (Iterator<YRpc<?>> ite = rpcs_inflight.values().iterator(); ite
        .hasNext();) {
      YRpc<?> rpc = ite.next();
      failOrRetryRpc(rpc, exception);
      ite.remove();
    }

    final ArrayList<YRpc<?>> rpcs;
    synchronized (this) {
      dead = true;
      rpcs = pending_rpcs;
      pending_rpcs = null;
    }
    if (rpcs != null) {
      failOrRetryRpcs(rpcs, exception);
    }
  }

  /**
   * Retry all the given RPCs.
   * @param rpcs a possibly empty but non-{@code null} collection of RPCs to retry or fail
   * @param exception an exception to propagate with the RPCs
   */
  private void failOrRetryRpcs(final Collection<YRpc<?>> rpcs,
                               final ConnectionResetException exception) {
    for (final YRpc<?> rpc : rpcs) {
      failOrRetryRpc(rpc, exception);
    }
  }

  /**
   * Retry the given RPC.
   * @param rpc an RPC to retry or fail
   * @param exception an exception to propagate with the RPC
   */
  private void failOrRetryRpc(final YRpc<?> rpc,
                              final ConnectionResetException exception) {
    AsyncYBClient.RemoteTablet tablet = rpc.getTablet();
    if (tablet == null) {  // Can't retry, dunno where this RPC should go.
      rpc.errback(exception);
    } else {
      ybClient.handleTabletNotFound(rpc, exception, this);
    }
  }



  @Override
  public void exceptionCaught(final ChannelHandlerContext ctx,
                              final Throwable cause) {
    final Channel c = ctx.channel();

    if (cause instanceof RejectedExecutionException) {
      LOG.warn(getPeerUuidLoggingString() + "RPC rejected by the executor," +
               " ignore this if we're shutting down", cause);
    } else if (cause instanceof ReadTimeoutException) {
      LOG.debug(getPeerUuidLoggingString() + "Encountered a read timeout");
      // Doing the cleanup here since we want to invalidate all the RPCs right _now_, and not let
      // the ReplayingDecoder continue decoding through Channels.close() below.
      cleanup(c);
    } else {
      LOG.debug(getPeerUuidLoggingString() + "Unexpected exception " + cause.getMessage() +
                " from downstream on " + c, cause);
    }
    if (c.isOpen()) {
      c.close();          // Will trigger channelClosed(), which will cleanup()
    } else {              // else: presumably a connection timeout.
      cleanup(c);         // => need to cleanup() from here directly.
    }
  }


  private ByteBuf connectionHeaderPreamble() {
    return Unpooled.wrappedBuffer(RPC_HEADER);
  }

  public void becomeReady(Channel chan) {
    this.chan = chan;
    sendQueuedRpcs();
  }

  /**
   * Sends the queued RPCs to the server, once we're connected to it.
   * This gets called after {@link #channelActive(ChannelHandlerContext)}, once we were able to
   * handshake with the server
   */
  private void sendQueuedRpcs() {
    ArrayList<YRpc<?>> rpcs;
    synchronized (this) {
      rpcs = pending_rpcs;
      pending_rpcs = null;
    }
    if (rpcs != null) {
      for (final YRpc<?> rpc : rpcs) {
        LOG.debug(getPeerUuidLoggingString() + "Executing RPC queued: " + rpc);
        sendRpc(rpc);
      }
    }
  }

  private String getPeerUuidLoggingString() {
    return "[Peer " + uuid + "] ";
  }

  /**
   * Returns this tablet server's uuid.
   * @return a string that contains this tablet server's uuid
   */
  String getUuid() {
    return uuid;
  }

  public String toString() {
    final StringBuilder buf = new StringBuilder(13 + 10 + 6 + 64 + 7 + 32 + 16 + 1 + 17 + 2 + 1);
    buf.append("TabletClient@")           // =13
        .append(hashCode())                 // ~10
        .append("(chan=")                   // = 6
        .append(chan)                       // ~64 (up to 66 when using IPv4)
        .append(", uuid=")                  // = 7
        .append(uuid)                       // = 32
        .append(", #pending_rpcs=");        // =16
    int npending_rpcs;
    synchronized (this) {
      npending_rpcs = pending_rpcs == null ? 0 : pending_rpcs.size();
    }
    buf.append(npending_rpcs);             // = 1
    buf.append(", #rpcs_inflight=")       // =17
        .append(rpcs_inflight.size())       // ~ 2
        .append(')');                       // = 1
    return buf.toString();
  }

  public static String getHostPort(SocketAddress remoteAddress) {
    if (remoteAddress == null) {
      return null;
    }
    if (!(remoteAddress instanceof InetSocketAddress)) {
      return "Unknown address type " + remoteAddress.getClass().getSimpleName();
    }
    InetSocketAddress inetSocketAddress = (InetSocketAddress) remoteAddress;
    return  inetSocketAddress.getAddress().getHostAddress() + ":" + inetSocketAddress.getPort();
  }

}
