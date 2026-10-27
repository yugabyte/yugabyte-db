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
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import io.fabric8.kubernetes.api.model.Secret;
import java.io.File;
import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardCopyOption;
import java.nio.file.attribute.PosixFilePermissions;
import java.security.cert.X509Certificate;
import java.time.Duration;
import java.time.Instant;
import java.util.Base64;
import java.util.Date;
import java.util.HashSet;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import javax.annotation.Nullable;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.FileUtils;
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
   * been synced for that CA and is currently valid.
   *
   * <p>The copy is per CA, so every universe on that CA presents it, including ones that don't
   * require a client cert. That is harmless while the cert is valid, but those servers still verify
   * a presented cert and would reject an expired one, so an expired copy is never presented.
   */
  public static Optional<Pair<String, String>> getClientCertFiles(@Nullable String caCertFile) {
    return getLocalPair(caCertFile).filter(pair -> isValidAt(pair.getLeft(), Instant.now()));
  }

  private static Optional<Pair<String, String>> getLocalPair(@Nullable String caCertFile) {
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
    // A null AZ reads the cluster-level gflags; per-AZ overrides can also set the flag.
    Set<UUID> azUuids = new HashSet<>(primary.placementInfo.getAllAZUUIDs());
    azUuids.add(null);
    boolean flagSet = false;
    for (UUID azUuid : azUuids) {
      for (ServerType serverType : new ServerType[] {ServerType.MASTER, ServerType.TSERVER}) {
        Map<String, String> gflags =
            GFlagsUtil.getGFlagsForAZ(azUuid, serverType, primary, details.clusters);
        flagSet |= Boolean.parseBoolean(gflags.getOrDefault(USE_CLIENT_CERTS_GFLAG, "false"));
      }
    }
    return flagSet
        && CertificateInfo.maybeGet(details.rootCA)
            .map(c -> c.getCertType() == CertConfigType.K8SCertManager)
            .orElse(false);
  }

  /**
   * Copies the client cert from the universe's Secret, waiting up to timeout for cert-manager to
   * fill it in. Errors other than a missing Secret are retried too, in case they are transient, and
   * the last one is the cause of the timeout.
   */
  public void sync(Universe universe, Duration timeout) {
    Instant deadline = Instant.now().plus(timeout);
    Exception lastError = null;
    while (true) {
      Optional<Pair<String, String>> pem = Optional.empty();
      try {
        pem = readSecret(universe);
      } catch (Exception e) {
        log.warn(
            "Failed to read client cert Secret {} of universe {}: {}",
            getSecretName(universe),
            universe.getName(),
            e.getMessage());
        lastError = e;
      }
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
                + universe.getName()
                + (lastError == null ? "" : ": " + lastError.getMessage()),
            lastError);
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
   * Re-copies the client cert if the local copy is missing or close to expiry, and removes a copy
   * that no universe on its CA requires any more. Never throws, so callers on a background or task
   * path are not failed by an unreachable cluster.
   */
  public void refreshIfNeeded(Universe universe) {
    try {
      if (!isClientCertRequired(universe)) {
        removeIfUnused(universe);
        return;
      }
      String caCertFile = universe.getCertificateNodetoNode();
      Optional<Pair<String, String>> local = getLocalPair(caCertFile);
      if (local.isPresent()
          && isValidAt(local.get().getLeft(), Instant.now().plus(REFRESH_BEFORE_EXPIRY))) {
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

  private static boolean isValidAt(String certFile, Instant instant) {
    try {
      X509Certificate cert = CertificateHelper.getCertificateFromFile(certFile);
      return !Date.from(instant).after(cert.getNotAfter())
          && !Date.from(Instant.now()).before(cert.getNotBefore());
    } catch (Exception e) {
      log.warn("Unreadable client cert {}", certFile, e);
      return false;
    }
  }

  // Once the universe that needed the copy is destroyed or drops the flag, nothing refreshes it, so
  // it would otherwise sit there until it expires.
  private void removeIfUnused(Universe universe) throws IOException {
    UUID rootCA = universe.getUniverseDetails().rootCA;
    String caCertFile = universe.getCertificateNodetoNode();
    if (rootCA == null
        || StringUtils.isBlank(caCertFile)
        || !Files.isDirectory(getClientCertDir(caCertFile))) {
      return;
    }
    boolean inUse =
        Customer.get(universe.getCustomerId()).getUniverses().stream()
            .anyMatch(u -> rootCA.equals(u.getUniverseDetails().rootCA) && isClientCertRequired(u));
    if (!inUse) {
      remove(caCertFile);
    }
  }

  private synchronized void remove(String caCertFile) throws IOException {
    Path dir = getClientCertDir(caCertFile);
    FileUtils.deleteDirectory(dir.toFile());
    log.info("Removed unused cert-manager client cert from {}", dir);
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
    } catch (RuntimeException e) {
      // The shell manager throws on a missing Secret, the native one returns null.
      if (StringUtils.contains(e.getMessage(), "Error from server (NotFound): secrets")) {
        return Optional.empty();
      }
      throw e;
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
    if (matches(dir.resolve(CertificateHelper.CLIENT_CERT), pem.getLeft())
        && matches(dir.resolve(CertificateHelper.CLIENT_KEY), pem.getRight())) {
      return;
    }
    // Not atomic as a pair: a client built between the two renames fails one handshake. If the
    // second rename fails, the next refresh sees the mismatch and rewrites both.
    replace(dir, CertificateHelper.CLIENT_KEY, pem.getRight());
    replace(dir, CertificateHelper.CLIENT_CERT, pem.getLeft());
    log.info("Synced cert-manager client cert into {}", dir);
  }

  private static boolean matches(Path file, String content) throws IOException {
    return Files.isRegularFile(file) && Files.readString(file).equals(content);
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
    return KubernetesUtil.getHelmFullName(releaseName) + "-client-tls";
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
