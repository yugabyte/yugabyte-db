// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.common;

import static org.mockito.AdditionalAnswers.delegatesTo;
import static org.mockito.Mockito.mock;

import com.typesafe.config.Config;
import com.yugabyte.yw.controllers.handlers.NodeAgentHandler;
import com.yugabyte.yw.forms.NodeAgentForm;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.nodeagent.NodeAgentGrpc;
import com.yugabyte.yw.nodeagent.Server.PingRequest;
import com.yugabyte.yw.nodeagent.Server.PingResponse;
import io.grpc.ManagedChannel;
import io.grpc.inprocess.InProcessChannelBuilder;
import io.grpc.inprocess.InProcessServerBuilder;
import io.grpc.testing.GrpcCleanupRule;
import java.io.IOException;
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

  // Graceful shutdown of the registered servers and their channels after the tests.
  @Rule public final GrpcCleanupRule grpcCleanup = new GrpcCleanupRule();

  private NodeAgentGrpc.NodeAgentImplBase serviceImpl;

  @Before
  public void setup() throws IOException {
    customer = ModelFactory.testCustomer();
    nodeAgentHandler = app.injector().instanceOf(NodeAgentHandler.class);
    NodeAgentForm payload = new NodeAgentForm();
    payload.version = "2.12.0";
    payload.name = "node";
    payload.ip = "10.20.30.40";
    nodeAgentHandler.enableConnectionValidation(false);
    nodeAgent = nodeAgentHandler.register(customer.uuid, payload);
    serviceImpl =
        mock(
            NodeAgentGrpc.NodeAgentImplBase.class,
            delegatesTo(
                new NodeAgentGrpc.NodeAgentImplBase() {
                  @Override
                  public void ping(
                      PingRequest request,
                      io.grpc.stub.StreamObserver<PingResponse> responseObserver) {
                    responseObserver.onNext(
                        PingResponse.newBuilder().setData(request.getData()).build());
                    responseObserver.onCompleted();
                  }
                }));
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

    nodeAgentClient = new NodeAgentClient(mock(Config.class), config -> channel);
  }

  @Test
  public void testPingSuccess() {
    nodeAgentClient.validateConnection(nodeAgent, true);
  }
}
