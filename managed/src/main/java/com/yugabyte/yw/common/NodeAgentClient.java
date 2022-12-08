// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.google.protobuf.ByteString;
import com.typesafe.config.Config;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.nodeagent.NodeAgentGrpc;
import com.yugabyte.yw.nodeagent.NodeAgentGrpc.NodeAgentBlockingStub;
import com.yugabyte.yw.nodeagent.NodeAgentGrpc.NodeAgentStub;
import com.yugabyte.yw.nodeagent.Server.DownloadFileRequest;
import com.yugabyte.yw.nodeagent.Server.DownloadFileResponse;
import com.yugabyte.yw.nodeagent.Server.ExecuteCommandRequest;
import com.yugabyte.yw.nodeagent.Server.ExecuteCommandResponse;
import com.yugabyte.yw.nodeagent.Server.FileInfo;
import com.yugabyte.yw.nodeagent.Server.PingRequest;
import com.yugabyte.yw.nodeagent.Server.UploadFileRequest;
import com.yugabyte.yw.nodeagent.Server.UploadFileResponse;
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
import io.grpc.stub.StreamObserver;
import io.jsonwebtoken.Jwts;
import io.jsonwebtoken.SignatureAlgorithm;
import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.Path;
import java.security.PrivateKey;
import java.time.Duration;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.Date;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import javax.inject.Singleton;
import javax.net.ssl.SSLException;
import lombok.Builder;
import lombok.Getter;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Http.Status;

/** This class contains all the methods required to communicate to a node agent server. */
@Slf4j
@Singleton
public class NodeAgentClient {
  public static final String NODE_AGENT_CONNECT_TIMEOUT_PROPERTY = "yb.node_agent.connect_timeout";
  public static final String NODE_AGENT_CLIENT_ENABLED_PROPERTY = "yb.node_agent.client.enabled";

  public static final int FILE_UPLOAD_CHUNK_SIZE_BYTES = 4096;

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
        Path caCertPath = nodeAgent.getCaCertFilePath();
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

  @Slf4j
  static class BaseResponseObserver<T> implements StreamObserver<T> {
    private final String id;
    private Throwable throwable;

    BaseResponseObserver(String id) {
      this.id = id;
    }

    @Override
    public void onNext(T response) {}

    @Override
    public void onError(Throwable throwable) {
      log.error("Error encountered for {}", getId(), throwable);
      this.throwable = throwable;
    }

    @Override
    public void onCompleted() {
      log.error("Completed for {}", getId());
    }

    protected String getId() {
      return id;
    }

    public Throwable getThrowable() {
      return throwable;
    }
  }

  static class ExecuteCommandResponseObserver extends BaseResponseObserver<ExecuteCommandResponse> {
    private final StringBuilder stdOut;
    private final StringBuilder stdErr;

    ExecuteCommandResponseObserver(String id) {
      super(id);
      this.stdOut = new StringBuilder();
      this.stdErr = new StringBuilder();
    }

    @Override
    public void onNext(ExecuteCommandResponse response) {
      if (response.hasError()) {
        stdErr.append(response.getError().getMessage());
        onError(
            new RuntimeException(
                String.format(
                    "Error(%d) %s",
                    response.getError().getCode(), response.getError().getMessage())));
      } else {
        stdOut.append(response.getOutput());
      }
    }

    public String getStdOut() {
      return stdOut.toString();
    }

    public String getStdErr() {
      return stdErr.toString();
    }
  }

  static class DownloadFileResponseObserver extends BaseResponseObserver<DownloadFileResponse> {
    private final OutputStream outputStream;

    DownloadFileResponseObserver(String id, OutputStream outputStream) {
      super(id);
      this.outputStream = outputStream;
    }

    @Override
    public void onNext(DownloadFileResponse response) {
      try {
        outputStream.write(response.getChunkData().toByteArray());
      } catch (IOException e) {
        onError(e);
      }
    }
  }

  public static String getNodeAgentJWT(NodeAgent nodeAgent) {
    PrivateKey privateKey = nodeAgent.getPrivateKey();
    return Jwts.builder()
        .setIssuer("https://www.yugabyte.com")
        .setSubject("Platform")
        .setIssuedAt(new Date())
        .setExpiration(Date.from(Instant.now().plus(15, ChronoUnit.MINUTES)))
        .signWith(SignatureAlgorithm.RS512, privateKey)
        .compact();
  }

  public static String getNodeAgentJWT(UUID nodeAgentUuid) {
    Optional<NodeAgent> nodeAgentOp = NodeAgent.maybeGet(nodeAgentUuid);
    if (!nodeAgentOp.isPresent()) {
      throw new RuntimeException(String.format("Node agent %s does not exist", nodeAgentUuid));
    }
    return getNodeAgentJWT(nodeAgentOp.get());
  }

  public static void addNodeAgentClientParams(NodeAgent nodeAgent, List<String> cmdParams) {
    addNodeAgentClientParams(nodeAgent, cmdParams, null);
  }

  public static void addNodeAgentClientParams(
      NodeAgent nodeAgent, List<String> cmdParams, Map<String, String> sensitiveCmdParams) {
    cmdParams.add("--node_agent_ip");
    cmdParams.add(nodeAgent.ip);
    cmdParams.add("--node_agent_port");
    cmdParams.add(String.valueOf(nodeAgent.port));
    cmdParams.add("--node_agent_cert_path");
    cmdParams.add(nodeAgent.getCaCertFilePath().toString());
    if (sensitiveCmdParams == null) {
      cmdParams.add("--node_agent_auth_token");
      cmdParams.add(getNodeAgentJWT(nodeAgent));
    } else {
      sensitiveCmdParams.put("--node_agent_auth_token", getNodeAgentJWT(nodeAgent));
    }
  }

  public Optional<NodeAgent> maybeGetNodeAgentClient(String ip) {
    // TODO check for server readiness?
    if (appConfig.getBoolean(NODE_AGENT_CLIENT_ENABLED_PROPERTY)) {
      return NodeAgent.maybeGetByIp(ip);
    }
    return Optional.empty();
  }

  private ManagedChannel getManagedChannel(NodeAgent nodeAgent, boolean enableTls) {
    Duration connectTimeout = appConfig.getDuration(NODE_AGENT_CONNECT_TIMEOUT_PROPERTY);
    ChannelConfig config =
        ChannelConfig.builder()
            .nodeAgent(nodeAgent)
            .enableTls(enableTls)
            .connectTimeout(connectTimeout)
            .build();
    return channelFactory.get(config);
  }

  public void validateConnection(NodeAgent nodeAgent, boolean enableTls) {
    ManagedChannel channel = getManagedChannel(nodeAgent, enableTls);
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

  public String executeCommand(NodeAgent nodeAgent, List<String> command) {
    ManagedChannel channel = getManagedChannel(nodeAgent, true);
    try {
      NodeAgentStub stub = NodeAgentGrpc.newStub(channel);
      String id = String.format("%s-%s", nodeAgent.uuid, command.get(0));
      ExecuteCommandResponseObserver responseObserver = new ExecuteCommandResponseObserver(id);
      ExecuteCommandRequest request =
          ExecuteCommandRequest.newBuilder().addAllCommand(command).build();
      stub.executeCommand(request, responseObserver);
      Throwable throwable = responseObserver.getThrowable();
      if (throwable != null) {
        log.error("Error in running command. Error: {}", responseObserver.stdErr);
        throw new RuntimeException(
            "Command execution failed. Error: " + throwable.getMessage(), throwable);
      }
      return responseObserver.getStdOut();
    } finally {
      channel.shutdownNow();
    }
  }

  public void uploadFile(NodeAgent nodeAgent, String inputFile, String outputFile) {
    ManagedChannel channel = getManagedChannel(nodeAgent, true);
    try (InputStream inputStream = new BufferedInputStream(new FileInputStream(inputFile))) {
      NodeAgentStub stub = NodeAgentGrpc.newStub(channel);
      String id = String.format("%s-%s", nodeAgent.uuid, inputFile);
      StreamObserver<UploadFileResponse> responseObserver = new BaseResponseObserver<>(id);
      StreamObserver<UploadFileRequest> requestOberver = stub.uploadFile(responseObserver);
      FileInfo fileInfo = FileInfo.newBuilder().setFilename(outputFile).build();
      UploadFileRequest request = UploadFileRequest.newBuilder().setFileInfo(fileInfo).build();
      // Send metadata first.
      requestOberver.onNext(request);
      byte[] bytes = new byte[FILE_UPLOAD_CHUNK_SIZE_BYTES];
      int bytesRead = 0;
      while ((bytesRead = inputStream.read(bytes)) > 0) {
        request =
            UploadFileRequest.newBuilder()
                .setChunkData(ByteString.copyFrom(bytes, 0, bytesRead))
                .build();
        requestOberver.onNext(request);
      }
      requestOberver.onCompleted();
    } catch (Exception e) {
      throw new RuntimeException(
          String.format(
              "Error in uploading file %s to %s. Error: %s", inputFile, outputFile, e.getMessage()),
          e);
    } finally {
      channel.shutdownNow();
    }
  }

  public void downloadFile(NodeAgent nodeAgent, String inputFile, String outputFile) {
    ManagedChannel channel = getManagedChannel(nodeAgent, true);
    try (OutputStream outputStream = new BufferedOutputStream(new FileOutputStream(outputFile))) {
      NodeAgentStub stub = NodeAgentGrpc.newStub(channel);
      String id = String.format("%s-%s", nodeAgent.uuid, outputFile);
      StreamObserver<DownloadFileResponse> responseObserver =
          new DownloadFileResponseObserver(id, outputStream);
      DownloadFileRequest request = DownloadFileRequest.newBuilder().setFilename(inputFile).build();
      stub.downloadFile(request, responseObserver);
    } catch (IOException e) {
      throw new RuntimeException(
          String.format(
              "Error in downloading file %s to %s. Error: %s",
              inputFile, outputFile, e.getMessage()),
          e);
    } finally {
      channel.shutdownNow();
    }
  }
}
