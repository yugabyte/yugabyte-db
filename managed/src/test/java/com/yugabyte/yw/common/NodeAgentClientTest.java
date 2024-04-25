// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.fail;
import static org.mockito.AdditionalAnswers.delegatesTo;
import static org.mockito.Mockito.mock;

import com.google.common.collect.ImmutableList;
import com.google.protobuf.ByteString;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.NodeAgentClient.NodeAgentUpgradeParam;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.controllers.handlers.NodeAgentHandler;
import com.yugabyte.yw.forms.NodeAgentForm;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeAgent.ArchType;
import com.yugabyte.yw.models.NodeAgent.OSType;
import com.yugabyte.yw.nodeagent.NodeAgentGrpc.NodeAgentImplBase;
import com.yugabyte.yw.nodeagent.Server.DownloadFileRequest;
import com.yugabyte.yw.nodeagent.Server.DownloadFileResponse;
import com.yugabyte.yw.nodeagent.Server.ExecuteCommandRequest;
import com.yugabyte.yw.nodeagent.Server.ExecuteCommandResponse;
import com.yugabyte.yw.nodeagent.Server.PingRequest;
import com.yugabyte.yw.nodeagent.Server.PingResponse;
import com.yugabyte.yw.nodeagent.Server.UpdateRequest;
import com.yugabyte.yw.nodeagent.Server.UpdateResponse;
import com.yugabyte.yw.nodeagent.Server.UploadFileRequest;
import com.yugabyte.yw.nodeagent.Server.UploadFileResponse;
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
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class NodeAgentClientTest extends FakeDBApplication {
  private NodeAgentClient nodeAgentClient;
  private NodeAgentHandler nodeAgentHandler;
  private Customer customer;
  private NodeAgent nodeAgent;
  private NodeAgentImplBase nodeAgentImpl;
  private UploadFileRequestObserver requestObserver;
  private NodeAgentImplBase serviceImpl;

  // Graceful shutdown of the registered servers and their channels after the tests.
  @Rule public final GrpcCleanupRule grpcCleanup = new GrpcCleanupRule();

  @Before
  public void setup() throws IOException {
    customer = ModelFactory.testCustomer();
    nodeAgentHandler = app.injector().instanceOf(NodeAgentHandler.class);
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
              responseObserver.onError(e);
            }
          }

          @Override
          public void update(
              UpdateRequest request, StreamObserver<UpdateResponse> responseObserver) {
            responseObserver.onNext(UpdateResponse.newBuilder().build());
            responseObserver.onCompleted();
          }
        };

    serviceImpl = mock(NodeAgentImplBase.class, delegatesTo(nodeAgentImpl));
    // Generate a unique in-process server name.
    String serverName = InProcessServerBuilder.generateName();

    // Create a server, add service, start, and register for automatic graceful shutdown.
    grpcCleanup.register(
        InProcessServerBuilder.forName(serverName)
            .directExecutor()
            .addService(serviceImpl)
            .build()
            .start());

    // Create a client channel and register for automatic graceful shutdown.
    ManagedChannel channel =
        grpcCleanup.register(InProcessChannelBuilder.forName(serverName).directExecutor().build());

    nodeAgentClient =
        new NodeAgentClient(mock(Config.class), mock(RuntimeConfGetter.class), config -> channel);
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
}
