// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.fail;
import static org.mockito.AdditionalAnswers.delegatesTo;
import static org.mockito.Mockito.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.Iterables;
import com.google.protobuf.ByteString;
import com.yugabyte.yw.commissioner.NodeAgentEnabler;
import com.yugabyte.yw.common.NodeAgentClient.ChannelFactory;
import com.yugabyte.yw.common.NodeAgentClient.NodeAgentUpgradeParam;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.controllers.handlers.NodeAgentHandler;
import com.yugabyte.yw.forms.NodeAgentForm;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeAgent.ArchType;
import com.yugabyte.yw.models.NodeAgent.OSType;
import com.yugabyte.yw.nodeagent.DescribeTaskRequest;
import com.yugabyte.yw.nodeagent.DescribeTaskResponse;
import com.yugabyte.yw.nodeagent.DownloadFileRequest;
import com.yugabyte.yw.nodeagent.DownloadFileResponse;
import com.yugabyte.yw.nodeagent.ExecuteCommandRequest;
import com.yugabyte.yw.nodeagent.ExecuteCommandResponse;
import com.yugabyte.yw.nodeagent.NodeAgentGrpc.NodeAgentImplBase;
import com.yugabyte.yw.nodeagent.NodeConfig;
import com.yugabyte.yw.nodeagent.PingRequest;
import com.yugabyte.yw.nodeagent.PingResponse;
import com.yugabyte.yw.nodeagent.PreflightCheckInput;
import com.yugabyte.yw.nodeagent.PreflightCheckOutput;
import com.yugabyte.yw.nodeagent.SubmitTaskRequest;
import com.yugabyte.yw.nodeagent.SubmitTaskResponse;
import com.yugabyte.yw.nodeagent.UpdateRequest;
import com.yugabyte.yw.nodeagent.UpdateResponse;
import com.yugabyte.yw.nodeagent.UploadFileRequest;
import com.yugabyte.yw.nodeagent.UploadFileResponse;
import io.grpc.ManagedChannel;
import io.grpc.inprocess.InProcessChannelBuilder;
import io.grpc.inprocess.InProcessServerBuilder;
import io.grpc.stub.StreamObserver;
import io.grpc.testing.GrpcCleanupRule;
import java.io.BufferedWriter;
import java.io.ByteArrayOutputStream;
import java.io.FileWriter;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Duration;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.UUID;
import java.util.function.Function;
import lombok.Getter;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@Slf4j
@RunWith(MockitoJUnitRunner.class)
public class NodeAgentClientTest extends FakeDBApplication {
  private NodeAgentClient nodeAgentClient;
  private NodeAgentHandler nodeAgentHandler;
  private Customer customer;
  private NodeAgent nodeAgent;
  private NodeAgentImplBase nodeAgentImpl;
  private UploadFileRequestObserver requestObserver;
  private NodeAgentImplBase mockServiceImpl;
  private RuntimeConfGetter mockConfGetter;
  private volatile AsyncTaskData asyncTaskData;

  // Graceful shutdown of the registered servers and their channels after the tests.
  @Rule public final GrpcCleanupRule grpcCleanup = new GrpcCleanupRule();

  @Getter
  @Setter
  static class AsyncTaskData {
    private volatile String taskId;
    private volatile Instant completionTime;
    private volatile Function<DescribeTaskRequest, DescribeTaskResponse> taskFunc;
  }

  @Before
  public void setup() throws IOException {
    customer = ModelFactory.testCustomer();
    nodeAgentHandler = app.injector().instanceOf(NodeAgentHandler.class);
    asyncTaskData = new AsyncTaskData();
    NodeAgentForm payload = new NodeAgentForm();
    payload.version = "2.12.0";
    payload.name = "node";
    payload.ip = "10.20.30.40";
    payload.osType = OSType.LINUX.name();
    payload.archType = ArchType.AMD64.name();
    payload.home = "/home/yugabyte/node-agent";
    nodeAgentHandler.enableConnectionValidation(false);
    nodeAgent = nodeAgentHandler.register(customer.getUuid(), payload);
    nodeAgentImpl =
        new NodeAgentImplBase() {
          @Override
          public void ping(PingRequest request, StreamObserver<PingResponse> responseObserver) {
            responseObserver.onNext(PingResponse.newBuilder().build());
            responseObserver.onCompleted();
          }

          @Override
          public void executeCommand(
              ExecuteCommandRequest request,
              StreamObserver<ExecuteCommandResponse> responseObserver) {
            responseObserver.onNext(
                ExecuteCommandResponse.newBuilder().setOutput(request.getCommand(1)).build());
            responseObserver.onCompleted();
          }

          @Override
          public StreamObserver<UploadFileRequest> uploadFile(
              StreamObserver<UploadFileResponse> responseObserver) {
            requestObserver = new UploadFileRequestObserver(responseObserver);
            return requestObserver;
          }

          @Override
          public void downloadFile(
              DownloadFileRequest request, StreamObserver<DownloadFileResponse> responseObserver) {
            try {
              byte[] bytes = Files.readAllBytes(Paths.get(request.getFilename()));
              responseObserver.onNext(
                  DownloadFileResponse.newBuilder()
                      .setChunkData(ByteString.copyFrom(bytes))
                      .build());
              responseObserver.onCompleted();
            } catch (IOException e) {
              log.error("Error occurred", e);
              responseObserver.onError(e);
            }
          }

          @Override
          public void update(
              UpdateRequest request, StreamObserver<UpdateResponse> responseObserver) {
            responseObserver.onNext(UpdateResponse.newBuilder().build());
            responseObserver.onCompleted();
          }

          @Override
          public void submitTask(
              SubmitTaskRequest request, StreamObserver<SubmitTaskResponse> responseObserver) {
            String taskId = UUID.randomUUID().toString();
            responseObserver.onNext(SubmitTaskResponse.newBuilder().setTaskId(taskId).build());
            asyncTaskData.setTaskId(taskId);
            responseObserver.onCompleted();
          }

          @Override
          public void describeTask(
              DescribeTaskRequest request, StreamObserver<DescribeTaskResponse> responseObserver) {
            try {
              String taskId = asyncTaskData.getTaskId();
              if (taskId == null || !taskId.equals(request.getTaskId())) {
                throw new IllegalArgumentException("Invalid task ID " + request.getTaskId());
              }
              while (Instant.now().isBefore(asyncTaskData.getCompletionTime())) {
                responseObserver.onNext(
                    DescribeTaskResponse.newBuilder()
                        .setOutput("Running at " + Instant.now())
                        .build());
                Thread.sleep(200);
              }
              responseObserver.onNext(asyncTaskData.getTaskFunc().apply(request));
              responseObserver.onCompleted();
            } catch (Exception e) {
              log.error("Error occurred", e);
              responseObserver.onError(e);
            }
          }
        };

    mockServiceImpl = mock(NodeAgentImplBase.class, delegatesTo(nodeAgentImpl));
    mockConfGetter = mock(RuntimeConfGetter.class);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.nodeAgentConnectionCacheSize)))
        .thenReturn(100);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.nodeAgentIgnoreConnectionCacheSize)))
        .thenReturn(false);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.nodeAgentDescribePollDeadline)))
        .thenReturn(Duration.ofSeconds(5));

    // Generate a unique in-process server name.
    String serverName = InProcessServerBuilder.generateName();

    // Create a server, add service, start, and register for automatic graceful shutdown.
    grpcCleanup.register(
        InProcessServerBuilder.forName(serverName)
            .directExecutor()
            .addService(mockServiceImpl)
            .build()
            .start());

    // Create a client channel and register for automatic graceful shutdown.
    ManagedChannel channel =
        grpcCleanup.register(
            InProcessChannelBuilder.forName(serverName)
                .directExecutor()
                .defaultServiceConfig(
                    ChannelFactory.getServiceConfig("node-agent/test_service_config.json"))
                .build());

    nodeAgentClient =
        new NodeAgentClient(
            mockConfGetter,
            com.google.inject.util.Providers.of(mock(NodeAgentEnabler.class)),
            config -> channel);
  }

  static class UploadFileRequestObserver implements StreamObserver<UploadFileRequest> {
    final StreamObserver<UploadFileResponse> responseObserver;
    ByteArrayOutputStream boa = new ByteArrayOutputStream();
    String filename = null;
    String data = null;

    UploadFileRequestObserver(StreamObserver<UploadFileResponse> responseObserver) {
      this.responseObserver = responseObserver;
    }

    @Override
    public void onNext(UploadFileRequest value) {
      if (value.hasFileInfo()) {
        filename = value.getFileInfo().getFilename();
      } else {
        try {
          boa.write(value.getChunkData().toByteArray());
        } catch (IOException e) {
          fail();
        }
      }
    }

    @Override
    public void onError(Throwable t) {}

    @Override
    public void onCompleted() {
      data = boa.toString();
      responseObserver.onNext(UploadFileResponse.newBuilder().build());
      responseObserver.onCompleted();
    }
  }

  @Test
  public void testPingSuccess() {
    nodeAgentClient.ping(nodeAgent);
  }

  @Test
  public void testExecuteCommand() {
    String output =
        nodeAgentClient
            .executeCommand(nodeAgent, ImmutableList.of("echo", "hello"))
            .processErrors()
            .getMessage();
    assertEquals("hello", output);
  }

  @Test
  public void testUploadFile() throws IOException {
    Path path = Files.createTempFile("na-upload", null);
    try (BufferedWriter writer = new BufferedWriter(new FileWriter(path.toFile()))) {
      writer.write("Testing");
    }
    try {
      nodeAgentClient.uploadFile(nodeAgent, path.toString(), "/tmp/test-upload-output");
      assertEquals("/tmp/test-upload-output", requestObserver.filename);
      assertEquals("Testing", requestObserver.data);
    } finally {
      path.toFile().delete();
    }
  }

  @Test
  public void testDownloadFile() throws IOException {
    Path path = Files.createTempFile("na-download", null);
    try (BufferedWriter writer = new BufferedWriter(new FileWriter(path.toFile()))) {
      writer.write("Testing");
    }
    try {
      nodeAgentClient.downloadFile(nodeAgent, path.toString(), "/tmp/test-download-output");
      byte[] bytes = Files.readAllBytes(Paths.get("/tmp/test-download-output"));
      assertEquals("Testing", new String(bytes).trim());
    } finally {
      path.toFile().delete();
    }
  }

  @Test
  public void testStartUpgrade() {
    NodeAgentUpgradeParam param =
        NodeAgentUpgradeParam.builder().certDir("test1").packagePath(Paths.get("/tmp/pkg")).build();
    nodeAgentClient.startUpgrade(nodeAgent, param);
  }

  @Test
  public void testFinalizeUpgrade() {
    nodeAgentClient.finalizeUpgrade(nodeAgent);
  }

  @Test
  public void testRunAsyncTask() {
    asyncTaskData.setTaskFunc(
        in ->
            DescribeTaskResponse.newBuilder()
                .setPreflightCheckOutput(
                    PreflightCheckOutput.newBuilder()
                        .addNodeConfigs(
                            NodeConfig.newBuilder().setType("DISK_SPACE").setValue("100GB").build())
                        .build())
                .build());
    asyncTaskData.setCompletionTime(Instant.now().plus(5, ChronoUnit.SECONDS));
    PreflightCheckOutput output =
        nodeAgentClient.runPreflightCheck(
            nodeAgent, PreflightCheckInput.newBuilder().build(), null /* user */);
    assertNotNull("PreflightCheckOutput must be set", output);
    NodeConfig nodeConfig = Iterables.getOnlyElement(output.getNodeConfigsList());
    assertEquals("DISK_SPACE", nodeConfig.getType());
    assertEquals("100GB", nodeConfig.getValue());
  }
}
