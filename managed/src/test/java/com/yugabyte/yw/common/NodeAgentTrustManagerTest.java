// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.nodeagent.NodeAgentGrpc;
import com.yugabyte.yw.nodeagent.PingRequest;
import com.yugabyte.yw.nodeagent.PingResponse;
import io.grpc.ManagedChannel;
import io.grpc.Server;
import io.grpc.Status;
import io.grpc.StatusRuntimeException;
import io.grpc.netty.shaded.io.grpc.netty.GrpcSslContexts;
import io.grpc.netty.shaded.io.grpc.netty.NettyChannelBuilder;
import io.grpc.netty.shaded.io.grpc.netty.NettyServerBuilder;
import io.grpc.netty.shaded.io.netty.handler.ssl.SslContextBuilder;
import io.grpc.stub.StreamObserver;
import io.grpc.testing.GrpcCleanupRule;
import java.math.BigInteger;
import java.net.InetSocketAddress;
import java.security.KeyPair;
import java.security.KeyPairGenerator;
import java.security.cert.X509Certificate;
import java.util.Collections;
import java.util.Date;
import java.util.concurrent.TimeUnit;
import javax.annotation.Nullable;
import org.bouncycastle.asn1.x500.X500Name;
import org.bouncycastle.asn1.x509.BasicConstraints;
import org.bouncycastle.asn1.x509.Extension;
import org.bouncycastle.asn1.x509.GeneralName;
import org.bouncycastle.asn1.x509.GeneralNames;
import org.bouncycastle.cert.X509v3CertificateBuilder;
import org.bouncycastle.cert.jcajce.JcaX509CertificateConverter;
import org.bouncycastle.cert.jcajce.JcaX509v3CertificateBuilder;
import org.bouncycastle.operator.jcajce.JcaContentSignerBuilder;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;

public class NodeAgentTrustManagerTest {
  private static final String HOST = "127.0.0.1";

  @Rule public final GrpcCleanupRule grpcCleanup = new GrpcCleanupRule();

  private KeyPair caKeyPair;
  private X509Certificate caCert;

  @Before
  public void setUp() throws Exception {
    caKeyPair = newKeyPair();
    caCert = newCert("CN=test-ca", caKeyPair, "CN=test-ca", caKeyPair, true, null);
  }

  @Test
  public void testCommonNameWithoutSan() throws Exception {
    ping(serverCert("CN=" + HOST, null), caCert);
  }

  @Test
  public void testIpSan() throws Exception {
    ping(serverCert("CN=node", new GeneralName(GeneralName.iPAddress, HOST)), caCert);
  }

  @Test
  public void testCommonNameMismatchWithoutSan() throws Exception {
    assertUnavailable(serverCert("CN=127.0.0.2", null), caCert);
  }

  @Test
  public void testCommonNameWithMismatchedSan() throws Exception {
    ping(serverCert("CN=" + HOST, new GeneralName(GeneralName.iPAddress, "127.0.0.2")), caCert);
  }

  @Test
  public void testCommonNameAndSanMismatch() throws Exception {
    assertUnavailable(
        serverCert("CN=127.0.0.3", new GeneralName(GeneralName.iPAddress, "127.0.0.2")), caCert);
  }

  @Test
  public void testMostSpecificCommonNameUsed() throws Exception {
    ping(serverCert("CN=other,CN=" + HOST, null), caCert);
    assertUnavailable(serverCert("CN=" + HOST + ",CN=other", null), caCert);
  }

  @Test
  public void testMatchPattern() {
    assertTrue(NodeAgentTrustManager.matchPattern("node1.example.com", "NODE1.example.com"));
    assertTrue(NodeAgentTrustManager.matchPattern("*.example.com", "node1.example.com"));
    assertTrue(NodeAgentTrustManager.matchPattern("node*.example.com", "node1.example.com"));
    assertFalse(NodeAgentTrustManager.matchPattern("*.example.com", "a.node1.example.com"));
    assertFalse(NodeAgentTrustManager.matchPattern("*.example.com", "example.com"));
    assertFalse(NodeAgentTrustManager.matchPattern("node1.example.com", "node2.example.com"));
  }

  @Test
  public void testCommonNameWithoutSanUntrustedCa() throws Exception {
    KeyPair otherCaKeyPair = newKeyPair();
    X509Certificate otherCaCert =
        newCert("CN=other-ca", otherCaKeyPair, "CN=other-ca", otherCaKeyPair, true, null);
    assertUnavailable(serverCert("CN=" + HOST, null), otherCaCert);
  }

  private void assertUnavailable(ServerCert serverCert, X509Certificate trustedCa) {
    StatusRuntimeException e =
        assertThrows(StatusRuntimeException.class, () -> ping(serverCert, trustedCa));
    assertEquals(Status.Code.UNAVAILABLE, e.getStatus().getCode());
  }

  private void ping(ServerCert serverCert, X509Certificate trustedCa) throws Exception {
    Server server =
        grpcCleanup.register(
            NettyServerBuilder.forAddress(new InetSocketAddress(HOST, 0))
                .sslContext(
                    GrpcSslContexts.configure(
                            SslContextBuilder.forServer(
                                serverCert.keyPair.getPrivate(), serverCert.cert))
                        .build())
                .addService(
                    new NodeAgentGrpc.NodeAgentImplBase() {
                      @Override
                      public void ping(
                          PingRequest request, StreamObserver<PingResponse> responseObserver) {
                        responseObserver.onNext(PingResponse.newBuilder().build());
                        responseObserver.onCompleted();
                      }
                    })
                .build()
                .start());
    ManagedChannel channel =
        grpcCleanup.register(
            NettyChannelBuilder.forAddress(HOST, server.getPort())
                .sslContext(
                    GrpcSslContexts.forClient()
                        .trustManager(
                            new NodeAgentTrustManager(Collections.singletonList(trustedCa)))
                        .build())
                .build());
    NodeAgentGrpc.newBlockingStub(channel)
        .withDeadlineAfter(10, TimeUnit.SECONDS)
        .ping(PingRequest.newBuilder().build());
  }

  private ServerCert serverCert(String subject, @Nullable GeneralName san) throws Exception {
    KeyPair keyPair = newKeyPair();
    return new ServerCert(keyPair, newCert(subject, keyPair, "CN=test-ca", caKeyPair, false, san));
  }

  private static KeyPair newKeyPair() throws Exception {
    KeyPairGenerator generator = KeyPairGenerator.getInstance("RSA");
    generator.initialize(2048);
    return generator.generateKeyPair();
  }

  private static X509Certificate newCert(
      String subject,
      KeyPair subjectKeyPair,
      String issuer,
      KeyPair issuerKeyPair,
      boolean isCa,
      @Nullable GeneralName san)
      throws Exception {
    long now = System.currentTimeMillis();
    X509v3CertificateBuilder builder =
        new JcaX509v3CertificateBuilder(
            new X500Name(issuer),
            BigInteger.valueOf(now),
            new Date(now - TimeUnit.HOURS.toMillis(1)),
            new Date(now + TimeUnit.DAYS.toMillis(1)),
            new X500Name(subject),
            subjectKeyPair.getPublic());
    builder.addExtension(Extension.basicConstraints, true, new BasicConstraints(isCa));
    if (san != null) {
      builder.addExtension(Extension.subjectAlternativeName, false, new GeneralNames(san));
    }
    return new JcaX509CertificateConverter()
        .getCertificate(
            builder.build(
                new JcaContentSignerBuilder("SHA256withRSA").build(issuerKeyPair.getPrivate())));
  }

  private static class ServerCert {
    final KeyPair keyPair;
    final X509Certificate cert;

    ServerCert(KeyPair keyPair, X509Certificate cert) {
      this.keyPair = keyPair;
      this.cert = cert;
    }
  }
}
