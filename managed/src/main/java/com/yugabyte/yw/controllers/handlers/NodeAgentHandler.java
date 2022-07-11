// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers.handlers;

import com.google.common.collect.ImmutableMap;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.controllers.JWTVerifier;
import com.yugabyte.yw.controllers.JWTVerifier.ClientType;
import com.yugabyte.yw.models.NodeAgent;
import io.ebean.annotation.Transactional;
import io.jsonwebtoken.Jwts;
import io.jsonwebtoken.SignatureAlgorithm;
import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.IOException;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.security.KeyPair;
import java.security.PrivateKey;
import java.security.PublicKey;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import java.time.Instant;
import java.util.Date;
import java.util.HashMap;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.FileUtils;
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.apache.commons.lang3.tuple.Pair;
import org.bouncycastle.asn1.x500.X500Name;
import org.bouncycastle.asn1.x509.GeneralName;
import org.bouncycastle.cert.jcajce.JcaX509CertificateHolder;
import play.mvc.Http.Status;

@Slf4j
@Singleton
public class NodeAgentHandler {
  public static final String NODE_AGENT_ID_CLAIM = "nodeAgentId";
  public static final String NODE_AGENT_USER_ID_CLAIM = "userId";
  public static final String NODE_AGENT_CUSTOMER_ID_CLAIM = "customerId";
  public static final String CLAIM_SESSION_PROPERTY = "jwt-claims";

  public static final String ROOT_CA_CERT_NAME = "ca.root.crt";
  public static final String ROOT_CA_KEY_NAME = "ca.key.pem";
  public static final String SERVER_CERT_NAME = "server.crt";
  public static final String SERVER_KEY_NAME = "server.key";
  public static final int CERT_EXPIRY_YEARS = 5;

  private final Config appConfig;

  @Inject
  public NodeAgentHandler(Config appConfig) {
    this.appConfig = appConfig;
  }

  private String getOrCreateBaseDirectory(NodeAgent nodeAgent) {
    Path nodeAgentsDirPath =
        Paths.get(
            appConfig.getString("yb.storage.path"),
            "node-agents",
            "certs",
            nodeAgent.customerUuid.toString(),
            nodeAgent.uuid.toString());
    log.info("Creating node agent base directory {}", nodeAgentsDirPath);
    return Util.getOrCreateDir(nodeAgentsDirPath);
  }

  private Pair<X509Certificate, KeyPair> createRootCert(
      NodeAgent nodeAgent, String certPath, String keyPath) {
    try {
      String certLabel = nodeAgent.uuid.toString();
      KeyPair keyPair = CertificateHelper.getKeyPairObject();
      X509Certificate x509 =
          CertificateHelper.generateCACertificate(certLabel, keyPair, CERT_EXPIRY_YEARS);
      CertificateHelper.writeCertFileContentToCertPath(x509, certPath);
      CertificateHelper.writeKeyFileContentToKeyPath(keyPair.getPrivate(), keyPath);
      return new ImmutablePair<>(x509, keyPair);
    } catch (RuntimeException e) {
      log.error("Failed to create root cert for node agent {}", nodeAgent.uuid, e);
      throw e;
    } catch (Exception e) {
      log.error("Failed to create root cert for node agent {}", nodeAgent.uuid, e);
      throw new RuntimeException(e.getMessage(), e);
    }
  }

  private Pair<X509Certificate, KeyPair> createServerCert(
      NodeAgent nodeAgent,
      X509Certificate caCert,
      PrivateKey caPrivateKey,
      String serverCertPath,
      String serverKeyPath) {
    try {
      Map<String, Integer> sans =
          ImmutableMap.<String, Integer>builder().put(nodeAgent.ip, GeneralName.iPAddress).build();

      // Add the security provider in case createSignedCertificate was never called.
      KeyPair keyPair = CertificateHelper.getKeyPairObject();
      // The first entry will be the certificate that needs to sign the necessary certificate.
      X500Name subject = new JcaX509CertificateHolder(caCert).getSubject();
      X509Certificate x509 =
          CertificateHelper.createAndSignCertificate(
              nodeAgent.ip, subject, keyPair, caCert, caPrivateKey, sans);
      CertificateHelper.writeCertFileContentToCertPath(x509, serverCertPath);
      CertificateHelper.writeKeyFileContentToKeyPath(keyPair.getPrivate(), serverKeyPath);
      return new ImmutablePair<>(x509, keyPair);
    } catch (RuntimeException e) {
      log.error("Failed to create server cert for node agent {}", nodeAgent.uuid, e);
      throw e;
    } catch (Exception e) {
      log.error("Failed to create server cert for node agent {}", nodeAgent.uuid, e);
      throw new RuntimeException(e.getMessage(), e);
    }
  }

  @Transactional
  private Pair<X509Certificate, KeyPair> generateNodeAgentCerts(NodeAgent nodeAgent) {
    try {
      Map<String, String> config = new HashMap<>();
      if (nodeAgent.config != null) {
        config.putAll(nodeAgent.config);
      }
      String basePath = getOrCreateBaseDirectory(nodeAgent);
      String caCertPath = Paths.get(basePath, ROOT_CA_CERT_NAME).toString();
      String caKeyPath = Paths.get(basePath, ROOT_CA_KEY_NAME).toString();
      String serverCertPath = Paths.get(basePath, SERVER_CERT_NAME).toString();
      String serverKeyPath = Paths.get(basePath, SERVER_KEY_NAME).toString();

      config.put(NodeAgent.ROOT_CA_CERT_PATH_PROPERTY, caCertPath);
      config.put(NodeAgent.ROOT_CA_KEY_PATH_PROPERTY, caKeyPath);
      config.put(NodeAgent.SERVER_CERT_PATH_PROPERTY, serverCertPath);
      config.put(NodeAgent.SERVER_KEY_PATH_PROPERTY, serverKeyPath);

      Pair<X509Certificate, KeyPair> pair = createRootCert(nodeAgent, caCertPath, caKeyPath);
      log.info(
          "Generated root cert for node agent: {} at key path: {} and cert path: {}",
          nodeAgent.uuid,
          caKeyPath,
          caCertPath);

      Pair<X509Certificate, KeyPair> serverPair =
          createServerCert(
              nodeAgent,
              pair.getLeft(),
              pair.getRight().getPrivate(),
              serverCertPath,
              serverKeyPath);

      log.info(
          "Generated self-signed server cert for node agent: {} at key path: {} and cert path: {}",
          nodeAgent.uuid,
          serverKeyPath,
          serverKeyPath);

      nodeAgent.saveConfig(config);
      return serverPair;
    } catch (RuntimeException e) {
      log.error("Failed to generate certs for node agent {}", nodeAgent.uuid, e);
      throw e;
    } catch (Exception e) {
      log.error("Failed to generate certs for node agent {}", nodeAgent.uuid, e);
      throw new RuntimeException(e.getMessage(), e);
    }
  }

  private PublicKey getPublicKeyFromCert(byte[] content) {
    try {
      CertificateFactory factory = CertificateFactory.getInstance("X.509");
      X509Certificate cert =
          (X509Certificate) factory.generateCertificate(new ByteArrayInputStream(content));
      return cert.getPublicKey();
    } catch (RuntimeException e) {
      throw e;
    } catch (Exception e) {
      throw new RuntimeException(e.getMessage(), e);
    }
  }

  private PrivateKey getPrivateKey(byte[] content) {
    return CertificateHelper.getPrivateKey(new String(content));
  }

  /**
   * Returns the public key of the given node agent.
   *
   * @param nodeAgentUuid node agent UUID.
   * @return
   */
  public PublicKey getNodeAgentPublicKey(UUID nodeAgentUuid) {
    Optional<NodeAgent> nodeAgentOp = NodeAgent.maybeGet(nodeAgentUuid);
    if (!nodeAgentOp.isPresent()) {
      throw new RuntimeException(String.format("Node agent %s does not exist", nodeAgentUuid));
    }
    return getPublicKeyFromCert(nodeAgentOp.get().getServerCert());
  }

  public PrivateKey getNodeAgentPrivateKey(UUID nodeAgentUuid) {
    Optional<NodeAgent> nodeAgentOp = NodeAgent.maybeGet(nodeAgentUuid);
    if (!nodeAgentOp.isPresent()) {
      throw new RuntimeException(String.format("Node agent %s does not exist", nodeAgentUuid));
    }
    return getPrivateKey(nodeAgentOp.get().getServerKey());
  }

  /**
   * Returns the JWT to authenticate a client request to the given node agent.
   *
   * @param nodeAgentUuid node agent UUID.
   * @return the JWT for sending request to the node agent.
   */
  public String getClientToken(UUID nodeAgentUuid, UUID userUuid) {
    PrivateKey privateKey = getNodeAgentPrivateKey(nodeAgentUuid);
    return Jwts.builder()
        .setIssuer("https://www.yugabyte.com")
        .setSubject(ClientType.NODE_AGENT.name())
        .setIssuedAt(Date.from(Instant.now()))
        .setExpiration(Date.from(Instant.now().plusSeconds(600)))
        .claim(JWTVerifier.CLIENT_ID_CLAIM, nodeAgentUuid.toString())
        .claim(JWTVerifier.USER_ID_CLAIM, userUuid.toString())
        .signWith(SignatureAlgorithm.RS256, privateKey)
        .compact();
  }

  /**
   * Registers the node agent to platform to set up the authentication keys.
   *
   * @param nodeAgent Partially populated node agent.
   * @return the fully populated node agent.
   */
  @Transactional
  public NodeAgent register(NodeAgent nodeAgent) {
    Optional<NodeAgent> nodeAgentOp = NodeAgent.maybeGetByIp(nodeAgent.ip);
    if (nodeAgentOp.isPresent()) {
      throw new PlatformServiceException(Status.BAD_REQUEST, "Node agent is already registered");
    }
    // Save within the transaction to get DB generated column values.
    nodeAgent.save();
    Pair<X509Certificate, KeyPair> serverPair = generateNodeAgentCerts(nodeAgent);
    X509Certificate serverCert = serverPair.getLeft();
    KeyPair serverKeyPair = serverPair.getRight();
    // Add the contents to the response.
    nodeAgent.config =
        ImmutableMap.<String, String>builder()
            .putAll(nodeAgent.config)
            .put(NodeAgent.SERVER_CERT_PROPERTY, CertificateHelper.getAsPemString(serverCert))
            .put(
                NodeAgent.SERVER_KEY_PROPERTY,
                CertificateHelper.getAsPemString(serverKeyPair.getPrivate()))
            .build();
    return nodeAgent;
  }

  /**
   * Returns the node agent with the given IDs.
   *
   * @param customerUuid customer UUID.
   * @param nodeAgentUuid node agent UUID.
   * @return the node agent.
   */
  public NodeAgent get(UUID customerUuid, UUID nodeAgentUuid) {
    return NodeAgent.getOrBadRequest(customerUuid, nodeAgentUuid);
  }

  /**
   * Unregisters the node agent from platform.
   *
   * @param uuid the node UUID.
   */
  public void unregister(UUID uuid) {
    NodeAgent.maybeGet(uuid)
        .ifPresent(
            nodeAgent -> {
              String basePath = getOrCreateBaseDirectory(nodeAgent);
              nodeAgent.delete();
              try {
                FileUtils.deleteDirectory(new File(basePath));
              } catch (IOException e) {
                log.error("Failed to clean up {}", basePath, e);
              }
            });
  }
}
