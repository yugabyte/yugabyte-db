// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.common;

import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.ImmutableMap;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.ConfigHelper.ConfigType;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.controllers.JWTVerifier;
import com.yugabyte.yw.controllers.JWTVerifier.ClientType;
import com.yugabyte.yw.models.FileData;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeAgent.ArchType;
import com.yugabyte.yw.models.NodeAgent.OSType;
import com.yugabyte.yw.models.NodeAgent.State;
import io.ebean.annotation.Transactional;
import io.jsonwebtoken.Jwts;
import io.jsonwebtoken.SignatureAlgorithm;
import java.io.BufferedInputStream;
import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileFilter;
import java.io.FileInputStream;
import java.io.FileReader;
import java.io.IOException;
import java.io.PrintWriter;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.security.KeyPair;
import java.security.PrivateKey;
import java.security.PublicKey;
import java.security.cert.X509Certificate;
import java.time.Instant;
import java.util.Date;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import javax.annotation.Nullable;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.Builder;
import lombok.Getter;
import lombok.NonNull;
import lombok.Singular;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.compress.archivers.tar.TarArchiveEntry;
import org.apache.commons.compress.archivers.tar.TarArchiveInputStream;
import org.apache.commons.compress.compressors.gzip.GzipCompressorInputStream;
import org.apache.commons.compress.utils.IOUtils;
import org.apache.commons.io.filefilter.WildcardFileFilter;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.apache.commons.lang3.tuple.Pair;
import org.bouncycastle.asn1.x500.X500Name;
import org.bouncycastle.asn1.x509.GeneralName;
import org.bouncycastle.cert.jcajce.JcaX509CertificateHolder;
import play.mvc.Http.Status;

@Slf4j
@Singleton
public class NodeAgentManager {
  public static final String NODE_AGENT_ID_CLAIM = "nodeAgentId";
  public static final String NODE_AGENT_USER_ID_CLAIM = "userId";
  public static final String NODE_AGENT_CUSTOMER_ID_CLAIM = "customerId";
  public static final String CLAIM_SESSION_PROPERTY = "jwt-claims";
  public static final String NODE_AGENT_RELEASES_PATH_PROPERTY = "yb.node_agent.releases.path";
  public static final String NODE_AGENT_SERVER_INSTALL_PROPERTY = "yb.node_agent.server.install";
  public static final int CERT_EXPIRY_YEARS = 5;
  public static final int NODE_AGENT_JWT_EXPIRY_SECS = 1800;

  private static final String NODE_AGENT_INSTALLER_FILE = "node-agent-installer.sh";
  private static final String NODE_AGENT_FILE_FILTER_FORMAT = "node_agent-%s*-%s-%s.tar.gz";
  private static final String NODE_AGENT_FILE_REGEX_FORMAT = "^node_agent-(.+)-%s-%s.tar.gz$";

  private final Config appConfig;
  private final ConfigHelper configHelper;

  private final CertificateHelper certificateHelper;

  @Inject
  public NodeAgentManager(
      Config appConfig, ConfigHelper configHelper, CertificateHelper certificateHelper) {
    this.appConfig = appConfig;
    this.configHelper = configHelper;
    this.certificateHelper = certificateHelper;
  }

  @Getter
  public static class CopyFileInfo {
    @NonNull private final Path sourcePath;
    @NonNull private final Path targetPath;
    private final String permission;

    CopyFileInfo(Path sourcePath, Path targetPath) {
      this(sourcePath, targetPath, null);
    }

    CopyFileInfo(Path sourcePath, Path targetPath, String permission) {
      this.sourcePath = sourcePath;
      this.targetPath = targetPath;
      this.permission = permission;
    }
  }

  /** Files needed to install or upgrade node agent. */
  @Builder
  @Getter
  public static class InstallerFiles {
    @NonNull private String certDir;
    @NonNull private Path packagePath;
    @Singular private List<Path> createDirs;
    @Singular private List<CopyFileInfo> copyFileInfos;
  }

  @VisibleForTesting
  public Path getNodeAgentBaseCertDirectory(NodeAgent nodeAgent) {
    return Paths.get(
        AppConfigHelper.getStoragePath(),
        "node-agent",
        "certs",
        nodeAgent.getCustomerUuid().toString(),
        nodeAgent.getUuid().toString());
  }

  private Path getOrCreateCertDirectory(NodeAgent nodeAgent, String certDir) {
    Path certDirPath = getNodeAgentBaseCertDirectory(nodeAgent).resolve(certDir);
    log.info("Creating node agent cert directory {}", certDirPath);
    return Util.getOrCreateDir(certDirPath);
  }

  // Certs are created in each directory <base-cert-dir>/<index>/.
  // The index keeps increasing.
  private Path getOrCreateNextCertDirectory(NodeAgent nodeAgent) {
    String certDirPath = nodeAgent.getConfig().getCertPath();
    if (StringUtils.isBlank(certDirPath)) {
      return getOrCreateCertDirectory(nodeAgent, "0");
    }
    String certDir = Paths.get(certDirPath).getFileName().toString();
    return getOrCreateCertDirectory(nodeAgent, String.valueOf(Integer.parseInt(certDir) + 1));
  }

  private Pair<X509Certificate, KeyPair> createRootCert(
      NodeAgent nodeAgent, String certPath, String keyPath) {
    try {
      String certLabel = nodeAgent.getUuid().toString();
      KeyPair keyPair = CertificateHelper.getKeyPairObject();
      X509Certificate x509 =
          certificateHelper.generateCACertificate(certLabel, keyPair, CERT_EXPIRY_YEARS);
      CertificateHelper.writeCertFileContentToCertPath(x509, certPath);
      CertificateHelper.writeKeyFileContentToKeyPath(keyPair.getPrivate(), keyPath);
      return new ImmutablePair<>(x509, keyPair);
    } catch (RuntimeException e) {
      log.error("Failed to create root cert for node agent {}", nodeAgent.getUuid(), e);
      throw e;
    } catch (Exception e) {
      log.error("Failed to create root cert for node agent {}", nodeAgent.getUuid(), e);
      throw new RuntimeException(e.getMessage(), e);
    }
  }

  private static Pair<X509Certificate, KeyPair> createServerCert(
      NodeAgent nodeAgent,
      X509Certificate caCert,
      PrivateKey caPrivateKey,
      String serverCertPath,
      String serverKeyPath,
      int expiryYrs) {
    try {
      Map<String, Integer> sans =
          ImmutableMap.<String, Integer>builder()
              .put(
                  nodeAgent.getIp(),
                  Util.isIpAddress(nodeAgent.getIp()) ? GeneralName.iPAddress : GeneralName.dNSName)
              .build();

      // Add the security provider in case createSignedCertificate was never called.
      KeyPair keyPair = CertificateHelper.getKeyPairObject();
      // The first entry will be the certificate that needs to sign the necessary certificate.
      X500Name subject = new JcaX509CertificateHolder(caCert).getSubject();
      X509Certificate x509 =
          CertificateHelper.createAndSignCertificate(
              nodeAgent.getIp(), subject, keyPair, caCert, caPrivateKey, sans, expiryYrs);
      CertificateHelper.writeCertFileContentToCertPath(x509, serverCertPath);
      CertificateHelper.writeKeyFileContentToKeyPath(keyPair.getPrivate(), serverKeyPath);
      return new ImmutablePair<>(x509, keyPair);
    } catch (RuntimeException e) {
      log.error("Failed to create server cert for node agent {}", nodeAgent.getUuid(), e);
      throw e;
    } catch (Exception e) {
      log.error("Failed to create server cert for node agent {}", nodeAgent.getUuid(), e);
      throw new RuntimeException(e.getMessage(), e);
    }
  }

  private Pair<X509Certificate, KeyPair> generateNodeAgentCerts(NodeAgent nodeAgent, Path dirPath) {
    try {
      String caCertPath = dirPath.resolve(NodeAgent.ROOT_CA_CERT_NAME).toString();
      String caKeyPath = dirPath.resolve(NodeAgent.ROOT_CA_KEY_NAME).toString();
      String serverCertPath = dirPath.resolve(NodeAgent.SERVER_CERT_NAME).toString();
      String serverKeyPath = dirPath.resolve(NodeAgent.SERVER_KEY_NAME).toString();
      int expiryYrs = appConfig.getInt("yb.tlsCertificate.server.maxLifetimeInYears");

      Pair<X509Certificate, KeyPair> pair = createRootCert(nodeAgent, caCertPath, caKeyPath);
      log.info(
          "Generated root cert for node agent: {} at key path: {} and cert path: {}",
          nodeAgent.getUuid(),
          caKeyPath,
          caCertPath);

      Pair<X509Certificate, KeyPair> serverPair =
          createServerCert(
              nodeAgent,
              pair.getLeft(),
              pair.getRight().getPrivate(),
              serverCertPath,
              serverKeyPath,
              expiryYrs);

      log.info(
          "Generated self-signed server cert for node agent: {} at key path: {} and cert path: {}",
          nodeAgent.getUuid(),
          serverKeyPath,
          serverKeyPath);
      return serverPair;
    } catch (RuntimeException e) {
      log.error("Failed to generate certs for node agent {}", nodeAgent.getUuid(), e);
      throw e;
    } catch (Exception e) {
      log.error("Failed to generate certs for node agent {}", nodeAgent.getUuid(), e);
      throw new RuntimeException(e.getMessage(), e);
    }
  }

  private void generateMergedNodeAgentCerts(NodeAgent nodeAgent, Path nextCertDirPath) {
    Path currCertFilepath = nodeAgent.getCaCertFilePath();
    Path nextCertFilepath = nextCertDirPath.resolve(NodeAgent.ROOT_CA_CERT_NAME);
    Path mergedCertFilepath = nextCertDirPath.resolve(NodeAgent.MERGED_ROOT_CA_CERT_NAME);
    log.info(
        "Creating merged cert file {} of curr {} and new {}",
        mergedCertFilepath,
        currCertFilepath,
        nextCertFilepath);
    try (PrintWriter writer = new PrintWriter(mergedCertFilepath.toFile())) {
      Path[] paths = new Path[] {currCertFilepath, nextCertFilepath};
      for (Path path : paths) {
        try (BufferedReader reader = new BufferedReader(new FileReader(path.toFile()))) {
          String line;
          while ((line = reader.readLine()) != null) {
            writer.println(line);
          }
        }
      }
    } catch (Exception e) {
      throw new RuntimeException(
          String.format(
              "Failed to merge two certificates %s and %s into %s. Error: %s",
              currCertFilepath, nextCertFilepath, mergedCertFilepath, e.getMessage()),
          e);
    }
  }

  /**
   * Returns the private key of the given node agent.
   *
   * @param nodeAgentUuid node agent UUID.
   * @return the private key.
   */
  public static PrivateKey getNodeAgentPrivateKey(UUID nodeAgentUuid) {
    Optional<NodeAgent> nodeAgentOp = NodeAgent.maybeGet(nodeAgentUuid);
    if (!nodeAgentOp.isPresent()) {
      throw new RuntimeException(String.format("Node agent %s does not exist", nodeAgentUuid));
    }
    return nodeAgentOp.get().getPrivateKey();
  }

  /**
   * Returns the public key of the given node agent.
   *
   * @param nodeAgentUuid node agent UUID.
   * @return the public key.
   */
  public static PublicKey getNodeAgentPublicKey(UUID nodeAgentUuid) {
    Optional<NodeAgent> nodeAgentOp = NodeAgent.maybeGet(nodeAgentUuid);
    if (!nodeAgentOp.isPresent()) {
      throw new RuntimeException(String.format("Node agent %s does not exist", nodeAgentUuid));
    }
    return nodeAgentOp.get().getPublicKey();
  }

  /**
   * Returns the JWT to authenticate a client request to the given node agent.
   *
   * @param nodeAgentUuid node agent UUID.
   * @return the JWT for sending request to the node agent.
   */
  @VisibleForTesting
  public String getClientToken(UUID nodeAgentUuid, UUID userUuid) {
    PrivateKey privateKey = getNodeAgentPrivateKey(nodeAgentUuid);
    return Jwts.builder()
        .setIssuer("https://www.yugabyte.com")
        .setSubject(ClientType.NODE_AGENT.name())
        .setExpiration(Date.from(Instant.now().plusSeconds(NODE_AGENT_JWT_EXPIRY_SECS)))
        .claim(JWTVerifier.CLIENT_ID_CLAIM.toString(), nodeAgentUuid.toString())
        .claim(JWTVerifier.USER_ID_CLAIM.toString(), userUuid.toString())
        .signWith(SignatureAlgorithm.RS256, privateKey)
        .compact();
  }

  /**
   * Generates the next new certs, keys and the merged certificates (old and new).
   *
   * @param nodeAgent the node agent.
   * @return path to the cert dir.
   */
  public Path generateCerts(NodeAgent nodeAgent) {
    nodeAgent.ensureState(State.UPGRADE);
    Path certDirPath = getOrCreateNextCertDirectory(nodeAgent);
    generateNodeAgentCerts(nodeAgent, certDirPath);
    generateMergedNodeAgentCerts(nodeAgent, certDirPath);
    return certDirPath;
  }

  // Returns the YBA software version.
  public String getSoftwareVersion() {
    return Objects.requireNonNull(
        (String) configHelper.getConfig(ConfigType.SoftwareVersion).get("version"));
  }

  /**
   * Returns the path to the node agent tgz package.
   *
   * @param osType node OS type.
   * @param archType node arch type.
   * @return the path to the node agent tgz package.
   */
  public Path getNodeAgentPackagePath(NodeAgent.OSType osType, NodeAgent.ArchType archType) {
    Path releasesPath = Paths.get(appConfig.getString(NODE_AGENT_RELEASES_PATH_PROPERTY));
    String softwareVersion = getSoftwareVersion();
    // An example from build job is node_agent-2.15.3.0-b1372-darwin-amd64.tar.gz.
    // But software version can also be like 2.17.1.0-PRE_RELEASE instead of the
    // actual build number.
    Pattern versionPattern = Pattern.compile(Util.YBA_VERSION_REGEX);
    Matcher matcher = versionPattern.matcher(softwareVersion);
    if (matcher.find()) {
      String pkgFileFilter =
          String.format(
              NODE_AGENT_FILE_FILTER_FORMAT,
              matcher.group(1),
              osType.name().toLowerCase(),
              archType.name().toLowerCase());
      Pattern filePattern =
          Pattern.compile(
              String.format(
                  NODE_AGENT_FILE_REGEX_FORMAT,
                  osType.name().toLowerCase(),
                  archType.name().toLowerCase()));
      // Search for a pattern like node_agent-2.15.3.0*-linux-amd64.tar.gz.
      FileFilter fileFilter = new WildcardFileFilter(pkgFileFilter);
      File[] files = releasesPath.toFile().listFiles(fileFilter);
      if (files != null) {
        for (File file : files) {
          matcher = filePattern.matcher(file.getName());
          if (matcher.find()) {
            // Extract the version with build number e.g. 2.15.3.0-b1372.
            String version = matcher.group(1);
            // Compare the full versions. The comparison ignores non-numeric build numbers.
            if (Util.compareYbVersions(softwareVersion, version, true) == 0) {
              return file.toPath();
            }
          }
        }
      }
    }
    throw new RuntimeException(
        String.format(
            "Node agent package with version %s, os %s and arch %s does not exist",
            softwareVersion, osType, archType));
  }

  /**
   * Returns the installer script in byte array. This is used for setup on the remote node.
   *
   * @return the byte array containing the script.
   */
  public byte[] getInstallerScript() {
    Path filepath = getNodeAgentPackagePath(OSType.LINUX, ArchType.AMD64);
    try (TarArchiveInputStream tarInput =
        new TarArchiveInputStream(
            new GzipCompressorInputStream(new FileInputStream(filepath.toFile())))) {
      TarArchiveEntry currEntry;
      while ((currEntry = tarInput.getNextTarEntry()) != null) {
        if (!currEntry.isFile() || !currEntry.getName().endsWith(NODE_AGENT_INSTALLER_FILE)) {
          continue;
        }
        BufferedInputStream in = new BufferedInputStream(tarInput);
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        IOUtils.copy(in, out);
        return out.toByteArray();
      }
    } catch (IOException e) {
      throw new PlatformServiceException(
          Status.INTERNAL_SERVER_ERROR, "Error in reading the node-agent installer.");
    }
    throw new PlatformServiceException(Status.NOT_FOUND, "Node-agent installer does not exist.");
  }

  /**
   * Create a node agent record in the DB.
   *
   * @param nodeAgent the node agent record to be created.
   * @return the updated node agent record along with cert and key in the config.
   */
  /**
   * Create a node agent record in the DB.
   *
   * @param nodeAgent nodeAgent the node agent record to be created.
   * @param includeCertContents if it is true, server cert and key contents are included.
   * @return the updated node agent record along with cert and key in the config.
   */
  @Transactional
  public NodeAgent create(NodeAgent nodeAgent, boolean includeCertContents) {
    nodeAgent.setConfig(new NodeAgent.Config());
    nodeAgent.setState(State.REGISTERING);
    nodeAgent.insert();
    Path certDirPath = getOrCreateNextCertDirectory(nodeAgent);
    Pair<X509Certificate, KeyPair> serverPair = generateNodeAgentCerts(nodeAgent, certDirPath);
    nodeAgent.getConfig().setCertPath(certDirPath.toString());
    nodeAgent.save();
    if (includeCertContents) {
      X509Certificate serverCert = serverPair.getLeft();
      KeyPair serverKeyPair = serverPair.getRight();
      nodeAgent.getConfig().setServerCert(CertificateHelper.getAsPemString(serverCert));
      nodeAgent
          .getConfig()
          .setServerKey(CertificateHelper.getAsPemString(serverKeyPair.getPrivate()));
    }
    return nodeAgent;
  }

  /**
   * Returns the installer files to be copied over to the remote node for installation or upgrade.
   * The files may be copied via node agent RPC for upgrade or over SSH/SCP for installation.
   *
   * @param nodeAgent nodeAgent the node agent record.
   * @param baseTargetDir Optional base directory on the target node. If it is null, the relative
   *     path is generated.
   * @return the installer files.
   */
  public InstallerFiles getInstallerFiles(NodeAgent nodeAgent, @Nullable Path baseTargetDir) {
    InstallerFiles.InstallerFilesBuilder builder = InstallerFiles.builder();
    // Package tgz file to be copied.
    Path packagePath = getNodeAgentPackagePath(nodeAgent.getOsType(), nodeAgent.getArchType());
    Path targetPackagePath = Paths.get("node-agent", "release", "node-agent.tgz");
    if (baseTargetDir != null) {
      targetPackagePath = baseTargetDir.resolve(targetPackagePath);
    }
    builder.packagePath(targetPackagePath);
    builder.copyFileInfo(new CopyFileInfo(packagePath, targetPackagePath));

    Path certDirPath = null;
    if (nodeAgent.getState() == State.REGISTERING) {
      builder.createDir(targetPackagePath.getParent());
      certDirPath = nodeAgent.getCertDirPath();
    } else {
      certDirPath = generateCerts(nodeAgent);
    }

    String targetCertDir = UUID.randomUUID().toString();
    builder.certDir(targetCertDir);

    // Cert file to be copied.
    Path targetCertDirPath = Paths.get("node-agent", "cert", targetCertDir);
    if (baseTargetDir != null) {
      targetCertDirPath = baseTargetDir.resolve(targetCertDirPath);
    }
    builder.createDir(targetCertDirPath);
    Path caCertPath = certDirPath.resolve(NodeAgent.SERVER_CERT_NAME);
    Path targetCaCertPath = targetCertDirPath.resolve("node_agent.crt");
    builder.copyFileInfo(new CopyFileInfo(caCertPath, targetCaCertPath));

    // Key file to be copied.
    Path keyPath = certDirPath.resolve(NodeAgent.SERVER_KEY_NAME);
    Path targetKeyPath = targetCertDirPath.resolve("node_agent.key");
    builder.copyFileInfo(new CopyFileInfo(keyPath, targetKeyPath));
    return builder.build();
  }

  /**
   * Purges the given node agent.
   *
   * @param nodeAgent the node agent.
   */
  public void purge(NodeAgent nodeAgent) {
    nodeAgent.purge(getNodeAgentBaseCertDirectory(nodeAgent));
  }

  /**
   * Replaces the existing node-agent certs with the new ones. This is invoked after the node agent
   * confirms that it has completed upgrade.
   *
   * @param nodeAgent the node agent.
   */
  public void replaceCerts(NodeAgent nodeAgent) {
    Path currentCertDirPath = nodeAgent.getCertDirPath();
    Path newCertDirPath = getOrCreateNextCertDirectory(nodeAgent);
    if (!Files.exists(newCertDirPath)) {
      throw new IllegalStateException(
          String.format(
              "New cert directory %s does not exist for node agent %s",
              newCertDirPath, nodeAgent.getUuid()));
    }
    // Point to the new directory and persist in the DB before deleting.
    log.info("Updating the cert dir to {} for node agent {}", newCertDirPath, nodeAgent.getUuid());
    nodeAgent.updateCertDirPath(newCertDirPath, State.UPGRADED);
    try {
      // Delete the old cert directory.
      log.info(
          "Deleting current cert dir {} for node agent {}",
          currentCertDirPath,
          nodeAgent.getUuid());
      FileData.deleteFiles(currentCertDirPath.toString(), true);
    } catch (Exception e) {
      // Ignore error.
      log.warn("Error deleting old cert directory {}", currentCertDirPath, e);
    }
  }

  /**
   * Perform post-upgrade cleanup.
   *
   * @param nodeAgent the node agent.
   */
  public void postUpgrade(NodeAgent nodeAgent) {
    // Get and delete the merged cert file if it exists.
    File mergedCaCertFile = nodeAgent.getMergedCaCertFilePath().toFile();
    if (mergedCaCertFile.exists()) {
      mergedCaCertFile.delete();
    }
  }
}
