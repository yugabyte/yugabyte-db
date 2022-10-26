// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.nodeagent.NodeAgentGrpc;
import com.yugabyte.yw.nodeagent.NodeAgentGrpc.NodeAgentBlockingStub;
import com.yugabyte.yw.nodeagent.Server.PingRequest;
import io.grpc.CallOptions;
import io.grpc.Channel;
import io.grpc.ClientCall;
import io.grpc.ClientInterceptor;
import io.grpc.ForwardingClientCall;
import io.grpc.ManagedChannel;
import io.grpc.Metadata;
import io.grpc.MethodDescriptor;
import io.grpc.netty.shaded.io.grpc.netty.GrpcSslContexts;
import io.grpc.netty.shaded.io.grpc.netty.NettyChannelBuilder;
import io.grpc.netty.shaded.io.netty.channel.ChannelOption;
import io.grpc.netty.shaded.io.netty.handler.ssl.SslContext;
import io.jsonwebtoken.Jwts;
import io.jsonwebtoken.SignatureAlgorithm;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.security.PrivateKey;
import java.time.Duration;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.Date;
import java.util.Optional;
import java.util.UUID;
import javax.inject.Singleton;
import javax.net.ssl.SSLException;
import lombok.Builder;
import lombok.Getter;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import play.mvc.Http.Status;

/** This class contains all the methods required to communicate to a node agent server. */
@Slf4j
@Singleton
public class NodeAgentClient {
  public static final String NODE_AGENT_CONNECT_TIMEOUT_PROPERTY = "yb.node_agent.connect_timeout";

  private final Config appConfig;
  private final ChannelFactory channelFactory;

  @Inject
  public NodeAgentClient(Config appConfig) {
    this(appConfig, null);
  }

  public NodeAgentClient(Config appConfig, ChannelFactory channelFactory) {
    this.appConfig = appConfig;
    this.channelFactory =
        channelFactory == null
            ? config -> ChannelFactory.getDefaultChannel(config)
            : channelFactory;
  }

  /** This class intercepts the client request to add the authorization token header. */
  @Slf4j
  public static class GrpcClientRequestInterceptor implements ClientInterceptor {
    private final UUID nodeAgentUuid;

    GrpcClientRequestInterceptor(UUID nodeAgentUuid) {
      this.nodeAgentUuid = nodeAgentUuid;
    }

    public <ReqT, RespT> ClientCall<ReqT, RespT> interceptCall(
        MethodDescriptor<ReqT, RespT> methodDescriptor, CallOptions callOptions, Channel channel) {

      return new ForwardingClientCall.SimpleForwardingClientCall<ReqT, RespT>(
          channel.newCall(methodDescriptor, callOptions)) {

        @Override
        public void start(ClientCall.Listener<RespT> responseListener, Metadata headers) {
          log.debug("Setting authorizaton token in header for node agent {}", nodeAgentUuid);
          String token = NodeAgentClient.getNodeAgentJWT(nodeAgentUuid);
          headers.put(Metadata.Key.of("authorization", Metadata.ASCII_STRING_MARSHALLER), token);
          super.start(responseListener, headers);
        }
      };
    }
  }

  @FunctionalInterface
  public interface ChannelFactory {
    ManagedChannel get(ChannelConfig config);

    static ManagedChannel getDefaultChannel(ChannelConfig config) {
      NodeAgent nodeAgent = config.nodeAgent;
      NettyChannelBuilder channelBuilder =
          NettyChannelBuilder.forAddress(nodeAgent.ip, nodeAgent.port);
      if (config.isEnableTls()) {
        Path caCertPath = getCertFilePath(nodeAgent, NodeAgent.ROOT_CA_CERT_NAME);
        try {
          SslContext sslcontext =
              GrpcSslContexts.forClient().trustManager(caCertPath.toFile()).build();
          channelBuilder = channelBuilder.sslContext(sslcontext);
          channelBuilder.intercept(new GrpcClientRequestInterceptor(nodeAgent.uuid));
        } catch (SSLException e) {
          throw new RuntimeException("SSL context creation for gRPC client failed", e);
        }
      } else {
        channelBuilder = channelBuilder.usePlaintext();
      }
      if (config.getConnectTimeout() != null) {
        channelBuilder.withOption(
            ChannelOption.CONNECT_TIMEOUT_MILLIS, (int) config.getConnectTimeout().toMillis());
      }
      return channelBuilder.build();
    }
  }

  @Getter
  @Builder
  public static class ChannelConfig {
    private NodeAgent nodeAgent;
    private boolean enableTls;
    private String certPath;
    private Duration connectTimeout;
  }

  private static String getNodeAgentJWT(UUID nodeAgentUuid) {
    Optional<NodeAgent> nodeAgentOp = NodeAgent.maybeGet(nodeAgentUuid);
    if (!nodeAgentOp.isPresent()) {
      throw new RuntimeException(String.format("Node agent %s does not exist", nodeAgentUuid));
    }
    PrivateKey privateKey = nodeAgentOp.get().getPrivateKey();
    return Jwts.builder()
        .setIssuer("https://www.yugabyte.com")
        .setSubject("Platform")
        .setIssuedAt(new Date())
        .setExpiration(Date.from(Instant.now().plus(15, ChronoUnit.MINUTES)))
        .signWith(SignatureAlgorithm.RS512, privateKey)
        .compact();
  }

  private static Path getCertFilePath(NodeAgent nodeAgent, String certName) {
    String certDirPath = nodeAgent.config.get(NodeAgent.CERT_DIR_PATH_PROPERTY);
    if (StringUtils.isBlank(certDirPath)) {
      throw new IllegalArgumentException(
          "Missing config key - " + NodeAgent.CERT_DIR_PATH_PROPERTY);
    }
    return Paths.get(certDirPath, certName);
  }

  public void validateConnection(NodeAgent nodeAgent, boolean enableTls) {
    Duration connectTimeout = appConfig.getDuration(NODE_AGENT_CONNECT_TIMEOUT_PROPERTY);
    ChannelConfig config =
        ChannelConfig.builder()
            .nodeAgent(nodeAgent)
            .enableTls(enableTls)
            .connectTimeout(connectTimeout)
            .build();
    ManagedChannel channel = channelFactory.get(config);
    try {
      log.info("Validating connectivity to node agent {}", nodeAgent.ip);
      NodeAgentBlockingStub stub = NodeAgentGrpc.newBlockingStub(channel);
      stub.ping(PingRequest.newBuilder().setData("test").build());
    } catch (Exception e) {
      throw new PlatformServiceException(Status.BAD_REQUEST, "Ping failed " + e.getMessage());
    } finally {
      channel.shutdownNow();
    }
  }
}
