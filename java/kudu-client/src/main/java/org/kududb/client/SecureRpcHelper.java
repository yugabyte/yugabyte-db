/*
 * Copyright (C) 2010-2012  The Async HBase Authors.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *   - Redistributions of source code must retain the aabove copyright notice,
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

import com.google.protobuf.ByteString;
import com.google.protobuf.ZeroCopyLiteralByteString;
import org.jboss.netty.buffer.ChannelBuffer;
import org.jboss.netty.buffer.ChannelBuffers;
import org.jboss.netty.channel.Channel;
import org.jboss.netty.channel.Channels;
import org.kududb.annotations.InterfaceAudience;
import org.kududb.rpc.RpcHeader;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import javax.security.auth.callback.Callback;
import javax.security.auth.callback.CallbackHandler;
import javax.security.auth.callback.NameCallback;
import javax.security.auth.callback.PasswordCallback;
import javax.security.auth.callback.UnsupportedCallbackException;
import javax.security.sasl.RealmCallback;
import javax.security.sasl.RealmChoiceCallback;
import javax.security.sasl.Sasl;
import javax.security.sasl.SaslClient;
import javax.security.sasl.SaslException;
import java.util.Map;
import java.util.TreeMap;

@InterfaceAudience.Private
public class SecureRpcHelper {

  public static final Logger LOG = LoggerFactory.getLogger(TabletClient.class);

  private final TabletClient client;
  private SaslClient saslClient;
  public static final String SASL_DEFAULT_REALM = "default";
  public static final Map<String, String> SASL_PROPS =
      new TreeMap<String, String>();
  private static final int SASL_CALL_ID = -33;
  private volatile boolean negoUnderway = true;
  private boolean useWrap = false; // no QOP at the moment

  public static final String USER_AND_PASSWORD = "java_client";

  public SecureRpcHelper(TabletClient client) {
    this.client = client;
    try {
      saslClient = Sasl.createSaslClient(new String[]{"PLAIN"
      }, null, null, SASL_DEFAULT_REALM,
          SASL_PROPS, new SaslClientCallbackHandler(USER_AND_PASSWORD, USER_AND_PASSWORD));
    } catch (SaslException e) {
      throw new RuntimeException("Could not create the SASL client", e);
    }
  }

  public void sendHello(Channel channel) {
    sendNegotiateMessage(channel);
  }

  private void sendNegotiateMessage(Channel channel) {
    RpcHeader.SaslMessagePB.Builder builder = RpcHeader.SaslMessagePB.newBuilder();
    builder.setState(RpcHeader.SaslMessagePB.SaslState.NEGOTIATE);
    sendSaslMessage(channel, builder.build());
  }

  private void sendSaslMessage(Channel channel, RpcHeader.SaslMessagePB msg) {
    RpcHeader.RequestHeader.Builder builder = RpcHeader.RequestHeader.newBuilder();
    builder.setCallId(SASL_CALL_ID);
    RpcHeader.RequestHeader header = builder.build();

    ChannelBuffer buffer = KuduRpc.toChannelBuffer(header, msg);
    Channels.write(channel, buffer);
  }

  public ChannelBuffer handleResponse(ChannelBuffer buf, Channel chan) throws SaslException {
    if (!saslClient.isComplete() || negoUnderway) {
      RpcHeader.SaslMessagePB response = parseSaslMsgResponse(buf);
      switch (response.getState()) {
        case NEGOTIATE:
          handleNegotiateResponse(chan, response);
          break;
        case CHALLENGE:
          handleChallengeResponse(chan, response);
          break;
        case SUCCESS:
          handleSuccessResponse(chan, response);
          break;
        default:
          System.out.println("Wrong sasl state");
      }
      return null;
    }
    return unwrap(buf);
  }

  /**
   * When QOP of auth-int or auth-conf is selected
   * This is used to unwrap the contents from the passed
   * buffer payload.
   */
  public ChannelBuffer unwrap(ChannelBuffer payload) {
    if(!useWrap) {
      return payload;
    }
    int len = payload.readInt();
    try {
      payload =
          ChannelBuffers.wrappedBuffer(saslClient.unwrap(payload.readBytes(len).array(), 0, len));
      return payload;
    } catch (SaslException e) {
      throw new IllegalStateException("Failed to unwrap payload", e);
    }
  }

  /**
   * When QOP of auth-int or auth-conf is selected
   * This is used to wrap the contents
   * into the proper payload (ie encryption, signature, etc)
   */
  public ChannelBuffer wrap(ChannelBuffer content) {
    if(!useWrap) {
      return content;
    }
    try {
      byte[] payload = new byte[content.writerIndex()];
      content.readBytes(payload);
      byte[] wrapped = saslClient.wrap(payload, 0, payload.length);
      ChannelBuffer ret = ChannelBuffers.wrappedBuffer(new byte[4 + wrapped.length]);
      ret.clear();
      ret.writeInt(wrapped.length);
      ret.writeBytes(wrapped);
      if (LOG.isDebugEnabled()) {
        LOG.debug("Wrapped payload: "+Bytes.pretty(ret));
      }
      return ret;
    } catch (SaslException e) {
      throw new IllegalStateException("Failed to wrap payload", e);
    }
  }

  private RpcHeader.SaslMessagePB parseSaslMsgResponse(ChannelBuffer buf) {
    CallResponse response = new CallResponse(buf);
    RpcHeader.ResponseHeader responseHeader = response.getHeader();
    int id = responseHeader.getCallId();
    if (id != SASL_CALL_ID) {
      throw new IllegalStateException("Received a call that wasn't for SASL");
    }

    RpcHeader.SaslMessagePB.Builder saslBuilder =  RpcHeader.SaslMessagePB.newBuilder();
    KuduRpc.readProtobuf(response.getPBMessage(), saslBuilder);
    return saslBuilder.build();
  }


  private void handleNegotiateResponse(Channel chan, RpcHeader.SaslMessagePB response) throws
      SaslException {
    RpcHeader.SaslMessagePB.SaslAuth negotiatedAuth = null;
    for (RpcHeader.SaslMessagePB.SaslAuth auth : response.getAuthsList()) {
      negotiatedAuth = auth;
    }
    byte[] saslToken = new byte[0];
    if (saslClient.hasInitialResponse())
      saslToken = saslClient.evaluateChallenge(saslToken);

    RpcHeader.SaslMessagePB.Builder builder = RpcHeader.SaslMessagePB.newBuilder();
    if (saslToken != null) {
      builder.setToken(ZeroCopyLiteralByteString.wrap(saslToken));
    }
    builder.setState(RpcHeader.SaslMessagePB.SaslState.INITIATE);
    builder.addAuths(negotiatedAuth);
    sendSaslMessage(chan, builder.build());

  }

  private void handleChallengeResponse(Channel chan, RpcHeader.SaslMessagePB response) throws
      SaslException {
    ByteString bs = response.getToken();
    byte[] saslToken = saslClient.evaluateChallenge(bs.toByteArray());
    if (saslToken == null) {
      throw new IllegalStateException("Not expecting an empty token");
    }
    RpcHeader.SaslMessagePB.Builder builder = RpcHeader.SaslMessagePB.newBuilder();
    builder.setToken(ZeroCopyLiteralByteString.wrap(saslToken));
    builder.setState(RpcHeader.SaslMessagePB.SaslState.RESPONSE);
    sendSaslMessage(chan, builder.build());
  }

  private void handleSuccessResponse(Channel chan, RpcHeader.SaslMessagePB response) {
    LOG.debug("nego finished");
    negoUnderway = false;
    client.sendContext(chan);
  }

  private static class SaslClientCallbackHandler implements CallbackHandler {
    private final String userName;
    private final char[] userPassword;

    public SaslClientCallbackHandler(String user, String password) {
      this.userName = user;
      this.userPassword = password.toCharArray();
    }

    public void handle(Callback[] callbacks)
        throws UnsupportedCallbackException {
      NameCallback nc = null;
      PasswordCallback pc = null;
      RealmCallback rc = null;
      for (Callback callback : callbacks) {
        if (callback instanceof RealmChoiceCallback) {
          continue;
        } else if (callback instanceof NameCallback) {
          nc = (NameCallback) callback;
        } else if (callback instanceof PasswordCallback) {
          pc = (PasswordCallback) callback;
        } else if (callback instanceof RealmCallback) {
          rc = (RealmCallback) callback;
        } else {
          throw new UnsupportedCallbackException(callback,
              "Unrecognized SASL client callback");
        }
      }
      if (nc != null) {
        nc.setName(userName);
      }
      if (pc != null) {
        pc.setPassword(userPassword);
      }
      if (rc != null) {
        rc.setText(rc.getDefaultText());
      }
    }
  }
}
