// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.certmgmt;

import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import io.fabric8.kubernetes.api.model.Secret;
import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardCopyOption;
import java.nio.file.attribute.PosixFilePermissions;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import java.time.Duration;
import java.time.Instant;
import java.util.Base64;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import javax.annotation.Nullable;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.lang3.tuple.Pair;

/**
 * Keeps a copy of the chart-issued cert-manager client cert next to the universe CA, so YBA can
 * present it on RPC connections when the universe sets
 * node_to_node_encryption_use_client_certificates. YBA holds no key for a K8SCertManager CA and
 * cannot sign a client cert itself.
 */
@Slf4j
@Singleton
public class CertManagerClientCertSync {

  public static final String CLIENT_CERT_SUBDIR = "yba-client";
  public static final String USE_CLIENT_CERTS_GFLAG =
      "node_to_node_encryption_use_client_certificates";

  private static final String SECRET_CERT_KEY = "tls.crt";
  private static final String SECRET_KEY_KEY = "tls.key";
  private static final String OLD_NAMING_SECRET_NAME = "yugabyte-tls-client-cert";
  // Matches the chart's default renewBefore; cert-manager has renewed by the time we get here.
  @VisibleForTesting static final Duration REFRESH_BEFORE_EXPIRY = Duration.ofDays(15);
  private static final Duration POLL_INTERVAL = Duration.ofSeconds(5);

  private final KubernetesManagerFactory kubernetesManagerFactory;

  @Inject
  public CertManagerClientCertSync(KubernetesManagerFactory kubernetesManagerFactory) {
    this.kubernetesManagerFactory = kubernetesManagerFactory;
  }

  /**
   * Returns the client cert and key to present for connections that trust caCertFile, if a copy has
   * been synced for that CA.
   */
  public static Optional<Pair<String, String>> getClientCertFiles(@Nullable String caCertFile) {
    if (StringUtils.isBlank(caCertFile)) {
      return Optional.empty();
    }
    File dir = getClientCertDir(caCertFile).toFile();
    File cert = new File(dir, CertificateHelper.CLIENT_CERT);
    File key = new File(dir, CertificateHelper.CLIENT_KEY);
    if (cert.isFile() && key.isFile()) {
      return Optional.of(Pair.of(cert.getAbsolutePath(), key.getAbsolutePath()));
    }
    return Optional.empty();
  }

  // A subdirectory, because Vault writes its own yugabytedb.crt beside the CA file.
  private static Path getClientCertDir(String caCertFile) {
    return Paths.get(caCertFile).toAbsolutePath().getParent().resolve(CLIENT_CERT_SUBDIR);
  }

  public static boolean isClientCertRequired(Universe universe) {
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    Cluster primary = details.getPrimaryCluster();
    if (!primary.userIntent.enableNodeToNodeEncrypt || details.rootCA == null) {
      return false;
    }
    boolean flagSet = false;
    for (ServerType serverType : new ServerType[] {ServerType.MASTER, ServerType.TSERVER}) {
      Map<String, String> gflags = GFlagsUtil.getBaseGFlags(serverType, primary, details.clusters);
      flagSet |= Boolean.parseBoolean(gflags.getOrDefault(USE_CLIENT_CERTS_GFLAG, "false"));
    }
    return flagSet
        && CertificateInfo.maybeGet(details.rootCA)
            .map(c -> c.getCertType() == CertConfigType.K8SCertManager)
            .orElse(false);
  }

  /**
   * Copies the client cert from the universe's Secret, waiting up to timeout for cert-manager to
   * fill it in. Throws if the Secret is not ready by then.
   */
  public void sync(Universe universe, Duration timeout) {
    Instant deadline = Instant.now().plus(timeout);
    while (true) {
      Optional<Pair<String, String>> pem = readSecret(universe);
      if (pem.isPresent()) {
        try {
          write(universe.getCertificateNodetoNode(), pem.get());
        } catch (IOException e) {
          throw new UncheckedIOException(e);
        }
        return;
      }
      if (Instant.now().isAfter(deadline)) {
        throw new RuntimeException(
            "Timed out waiting for client cert Secret "
                + getSecretName(universe)
                + " of universe "
                + universe.getName());
      }
      try {
        Thread.sleep(POLL_INTERVAL.toMillis());
      } catch (InterruptedException e) {
        Thread.currentThread().interrupt();
        throw new RuntimeException(e);
      }
    }
  }

  /**
   * Re-copies the client cert if the local copy is missing or close to expiry. Never throws, so
   * callers on a background or task path are not failed by an unreachable cluster.
   */
  public void refreshIfNeeded(Universe universe) {
    try {
      if (!isClientCertRequired(universe)) {
        return;
      }
      String caCertFile = universe.getCertificateNodetoNode();
      Optional<Pair<String, String>> local = getClientCertFiles(caCertFile);
      if (local.isPresent() && !expiresSoon(local.get().getLeft())) {
        return;
      }
      Optional<Pair<String, String>> pem = readSecret(universe);
      if (pem.isEmpty()) {
        // Expected while a universe is being created, before helm install.
        log.info(
            "Client cert Secret {} of universe {} is not ready",
            getSecretName(universe),
            universe.getName());
        return;
      }
      write(caCertFile, pem.get());
    } catch (Exception e) {
      log.warn("Failed to refresh client cert of universe {}", universe.getName(), e);
    }
  }

  private static boolean expiresSoon(String certFile) {
    try (ByteArrayInputStream in =
        new ByteArrayInputStream(Files.readAllBytes(Paths.get(certFile)))) {
      X509Certificate cert =
          (X509Certificate) CertificateFactory.getInstance("X.509").generateCertificate(in);
      return cert.getNotAfter().toInstant().isBefore(Instant.now().plus(REFRESH_BEFORE_EXPIRY));
    } catch (Exception e) {
      log.warn("Unreadable client cert {}, refreshing it", certFile, e);
      return true;
    }
  }

  private Optional<Pair<String, String>> readSecret(Universe universe) {
    Cluster primary = universe.getUniverseDetails().getPrimaryCluster();
    Map<UUID, Map<String, String>> azToConfig =
        KubernetesUtil.getConfigPerAZ(primary.placementInfo);
    // Every AZ's Secret is signed by the same CA, so the first one is enough.
    Map.Entry<UUID, Map<String, String>> az = azToConfig.entrySet().iterator().next();
    String namespace = getNamespace(universe, az.getKey(), az.getValue());
    String secretName = getSecretName(universe, az.getKey());
    Secret secret;
    try {
      secret =
          kubernetesManagerFactory.getManager().getSecret(az.getValue(), secretName, namespace);
    } catch (Exception e) {
      // The shell manager throws on a missing Secret, the native one returns null.
      log.debug("Could not read Secret {}/{}: {}", namespace, secretName, e.getMessage());
      return Optional.empty();
    }
    if (secret == null || secret.getData() == null) {
      return Optional.empty();
    }
    String cert = secret.getData().get(SECRET_CERT_KEY);
    String key = secret.getData().get(SECRET_KEY_KEY);
    if (StringUtils.isAnyBlank(cert, key)) {
      return Optional.empty();
    }
    Base64.Decoder decoder = Base64.getDecoder();
    return Optional.of(Pair.of(new String(decoder.decode(cert)), new String(decoder.decode(key))));
  }

  // Serialized because a health check and a task can refresh the same CA directory at once.
  private synchronized void write(String caCertFile, Pair<String, String> pem) throws IOException {
    Path dir = getClientCertDir(caCertFile);
    Files.createDirectories(dir);
    Path certPath = dir.resolve(CertificateHelper.CLIENT_CERT);
    if (Files.exists(certPath) && Files.readString(certPath).equals(pem.getLeft())) {
      return;
    }
    // Not atomic as a pair: a client built between the two renames fails one handshake, which
    // happens at most once per cert-manager renewal.
    replace(dir, CertificateHelper.CLIENT_KEY, pem.getRight());
    replace(dir, CertificateHelper.CLIENT_CERT, pem.getLeft());
    log.info("Synced cert-manager client cert into {}", dir);
  }

  private static void replace(Path dir, String name, String content) throws IOException {
    Path tmp =
        Files.createTempFile(
            dir,
            name,
            ".tmp",
            PosixFilePermissions.asFileAttribute(PosixFilePermissions.fromString("rw-------")));
    try {
      Files.writeString(tmp, content);
      Files.move(
          tmp,
          dir.resolve(name),
          StandardCopyOption.ATOMIC_MOVE,
          StandardCopyOption.REPLACE_EXISTING);
    } finally {
      Files.deleteIfExists(tmp);
    }
  }

  private static String getSecretName(Universe universe) {
    Cluster primary = universe.getUniverseDetails().getPrimaryCluster();
    UUID azUuid = KubernetesUtil.getConfigPerAZ(primary.placementInfo).keySet().iterator().next();
    return getSecretName(universe, azUuid);
  }

  @VisibleForTesting
  static String getSecretName(Universe universe, UUID azUuid) {
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    if (!details.useNewHelmNamingStyle) {
      return OLD_NAMING_SECRET_NAME;
    }
    String releaseName =
        KubernetesUtil.getHelmReleaseName(
            isMultiAz(universe),
            details.nodePrefix,
            universe.getName(),
            AvailabilityZone.getOrBadRequest(azUuid).getCode(),
            false /* isReadOnlyCluster */,
            true /* newNamingStyle */);
    // The chart's yugabyte.fullname: trunc 43 | trimSuffix "-".
    String fullName = StringUtils.removeEnd(StringUtils.substring(releaseName, 0, 43), "-");
    return fullName + "-client-tls";
  }

  private static String getNamespace(Universe universe, UUID azUuid, Map<String, String> azConfig) {
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    return KubernetesUtil.getKubernetesNamespace(
        isMultiAz(universe),
        details.nodePrefix,
        AvailabilityZone.getOrBadRequest(azUuid).getCode(),
        azConfig,
        details.useNewHelmNamingStyle,
        false /* isReadOnlyCluster */);
  }

  private static boolean isMultiAz(Universe universe) {
    Cluster primary = universe.getUniverseDetails().getPrimaryCluster();
    return PlacementInfoUtil.isMultiAZ(
        Provider.getOrBadRequest(UUID.fromString(primary.userIntent.provider)));
  }
}
