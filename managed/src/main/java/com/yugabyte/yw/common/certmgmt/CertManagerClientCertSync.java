// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.certmgmt;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.AppConfigHelper;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.helm.HelmUtils;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CertificateInfo;
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
import java.security.cert.CertificateException;
import java.security.cert.X509Certificate;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Base64;
import java.util.Date;
import java.util.HashSet;
import java.util.List;
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
 * Keeps a per-universe copy of the chart-issued cert-manager client cert, so YBA can present it on
 * RPC connections to a universe that sets node_to_node_encryption_use_client_certificates. YBA
 * holds no key for a K8SCertManager CA and cannot sign a client cert itself.
 *
 * <p>The copy is presented only to the universe it was synced for, and only while that universe
 * requires it: a server that doesn't require a client cert still checks one it is offered, against
 * required_uid and verify_client_endpoint among others.
 */
@Slf4j
@Singleton
public class CertManagerClientCertSync {

  public static final String CLIENT_CERT_DIR = "yba-client-certs";
  public static final String USE_CLIENT_CERTS_GFLAG =
      "node_to_node_encryption_use_client_certificates";

  private static final String SECRET_CERT_KEY = "tls.crt";
  private static final String SECRET_KEY_KEY = "tls.key";
  private static final String OLD_NAMING_SECRET_NAME = "yugabyte-tls-client-cert";
  private static final String KEY_ALGORITHM_OVERRIDE = "tls.certManager.certificates.algorithm";
  // AsyncYBClient loads the client key with KeyFactory.getInstance("RSA").
  private static final String SUPPORTED_KEY_ALGORITHM = "RSA";
  // Matches the chart's default renewBefore; cert-manager has renewed by the time we get here.
  @VisibleForTesting static final Duration REFRESH_BEFORE_EXPIRY = Duration.ofDays(15);
  private static final Duration POLL_INTERVAL = Duration.ofSeconds(5);

  private final KubernetesManagerFactory kubernetesManagerFactory;

  @Inject
  public CertManagerClientCertSync(KubernetesManagerFactory kubernetesManagerFactory) {
    this.kubernetesManagerFactory = kubernetesManagerFactory;
  }

  /**
   * Returns the client cert and key to present on connections to universe, if it requires one and a
   * valid copy has been synced.
   */
  public static Optional<Pair<String, String>> getClientCertFiles(Universe universe) {
    // The file check first: it is cheaper than evaluating gflags on every client build.
    return getLocalPair(universe.getUniverseUUID())
        .filter(pair -> isClientCertRequired(universe))
        .filter(pair -> isValidAt(pair.getLeft(), Instant.now()));
  }

  private static Optional<Pair<String, String>> getLocalPair(UUID universeUUID) {
    File dir = getClientCertDir(universeUUID).toFile();
    File cert = new File(dir, CertificateHelper.CLIENT_CERT);
    File key = new File(dir, CertificateHelper.CLIENT_KEY);
    if (cert.isFile() && key.isFile()) {
      return Optional.of(Pair.of(cert.getAbsolutePath(), key.getAbsolutePath()));
    }
    return Optional.empty();
  }

  // Outside certs/, so CA directories, which attach/detach and CA deletion walk, hold only CA
  // files.
  @VisibleForTesting
  public static Path getClientCertDir(UUID universeUUID) {
    return Paths.get(AppConfigHelper.getStoragePath(), CLIENT_CERT_DIR, universeUUID.toString());
  }

  /** Removes the synced copy of a universe, when it is destroyed or no longer requires one. */
  public static void deleteClientCert(UUID universeUUID) {
    synchronized (CertManagerClientCertSync.class) {
      Path dir = getClientCertDir(universeUUID);
      if (Files.isDirectory(dir)) {
        FileUtils.deleteQuietly(dir.toFile());
        log.info("Removed cert-manager client cert from {}", dir);
      }
    }
  }

  public static boolean isClientCertRequired(Universe universe) {
    return isClientCertRequired(universe, universe.getUniverseDetails().rootCA);
  }

  /** As above, but as if the universe used rootCA, for a task that is about to switch to it. */
  public static boolean isClientCertRequired(Universe universe, @Nullable UUID rootCA) {
    return isClientCertRequired(universe.getUniverseDetails(), rootCA);
  }

  private static boolean isClientCertRequired(
      UniverseDefinitionTaskParams details, @Nullable UUID rootCA) {
    Cluster primary = details.getPrimaryCluster();
    if (!primary.userIntent.enableNodeToNodeEncrypt || rootCA == null) {
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
        && CertificateInfo.maybeGet(rootCA)
            .map(c -> c.getCertType() == CertConfigType.K8SCertManager)
            .orElse(false);
  }

  /**
   * Rejects helm overrides that make the chart issue a non-RSA client key for a universe that needs
   * one: the YB Java client only loads RSA keys.
   */
  public static void validateHelmOverrides(
      UniverseDefinitionTaskParams details,
      @Nullable String universeOverrides,
      @Nullable Map<String, String> azOverrides) {
    if (!isClientCertRequired(details, details.rootCA)) {
      return;
    }
    List<String> overrides = new ArrayList<>();
    overrides.add(universeOverrides);
    if (azOverrides != null) {
      overrides.addAll(azOverrides.values());
    }
    for (String overridesYaml : overrides) {
      String algorithm;
      try {
        algorithm =
            HelmUtils.flattenMap(HelmUtils.convertYamlToMap(overridesYaml))
                .get(KEY_ALGORITHM_OVERRIDE);
      } catch (Exception e) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Kubernetes overrides are not valid YAML: " + e.getMessage());
      }
      if (algorithm != null && !SUPPORTED_KEY_ALGORITHM.equalsIgnoreCase(algorithm)) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "%s=%s is not supported with %s: YugabyteDB Anywhere can only present an %s client"
                    + " certificate",
                KEY_ALGORITHM_OVERRIDE,
                algorithm,
                USE_CLIENT_CERTS_GFLAG,
                SUPPORTED_KEY_ALGORITHM));
      }
    }
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
        checkKeyAlgorithm(universe, pem.get());
        try {
          write(universe.getUniverseUUID(), pem.get());
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
   * Re-copies the client cert if the local copy is missing, doesn't match, or is close to expiry,
   * and removes it once the universe no longer requires one. Never throws, so callers on a
   * background or task path are not failed by an unreachable cluster.
   */
  public void refreshIfNeeded(Universe snapshot) {
    // A queued health check carries the universe as it was when scheduled; it may since have been
    // deleted or detached, with its Secret still readable.
    Optional<Universe> current = Universe.maybeGet(snapshot.getUniverseUUID());
    if (current.isEmpty()) {
      return;
    }
    Universe universe = current.get();
    try {
      if (!isClientCertRequired(universe)) {
        deleteClientCert(universe.getUniverseUUID());
        return;
      }
      Optional<Pair<String, String>> local = getLocalPair(universe.getUniverseUUID());
      if (local.isPresent()
          && isValidAt(local.get().getLeft(), Instant.now().plus(REFRESH_BEFORE_EXPIRY))
          && isMatchingPair(local.get())) {
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
      checkKeyAlgorithm(universe, pem.get());
      writeIfUniverseExists(universe.getUniverseUUID(), pem.get());
    } catch (Exception e) {
      log.warn("Failed to refresh client cert of universe {}", universe.getName(), e);
    }
  }

  // Checked under the lock deleteClientCert holds, so a delete can't land between check and write.
  private static void writeIfUniverseExists(UUID universeUUID, Pair<String, String> pem)
      throws IOException {
    synchronized (CertManagerClientCertSync.class) {
      if (Universe.maybeGet(universeUUID).isEmpty()) {
        log.info("Universe {} was deleted; not writing its client cert", universeUUID);
        return;
      }
      writeLocked(getClientCertDir(universeUUID), pem);
    }
  }

  // Catches an algorithm set outside YBA's overrides, which the precheck doesn't see.
  private static void checkKeyAlgorithm(Universe universe, Pair<String, String> pem) {
    String algorithm;
    try {
      algorithm =
          CertificateHelper.convertStringToX509Cert(pem.getLeft()).getPublicKey().getAlgorithm();
    } catch (CertificateException e) {
      throw new RuntimeException(
          "Client cert Secret " + getSecretName(universe) + " holds an unreadable cert", e);
    }
    if (!SUPPORTED_KEY_ALGORITHM.equals(algorithm)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Client cert Secret %s of universe %s has a %s key; YugabyteDB Anywhere can only"
                  + " present an %s client certificate",
              getSecretName(universe), universe.getName(), algorithm, SUPPORTED_KEY_ALGORITHM));
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

  // Catches a key left over from a half-finished write without reading the Secret on every check.
  private static boolean isMatchingPair(Pair<String, String> files) {
    try {
      return CertificateHelper.verifySignature(
          CertificateHelper.getCertificateFromFile(files.getLeft()),
          Files.readString(Paths.get(files.getRight())));
    } catch (Exception e) {
      log.warn("Unreadable client key {}", files.getRight(), e);
      return false;
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

  // Serialized because a health check and a task can refresh the same universe at once.
  private static void write(UUID universeUUID, Pair<String, String> pem) throws IOException {
    synchronized (CertManagerClientCertSync.class) {
      writeLocked(getClientCertDir(universeUUID), pem);
    }
  }

  private static void writeLocked(Path dir, Pair<String, String> pem) throws IOException {
    Files.createDirectories(dir);
    if (matches(dir.resolve(CertificateHelper.CLIENT_CERT), pem.getLeft())
        && matches(dir.resolve(CertificateHelper.CLIENT_KEY), pem.getRight())) {
      return;
    }
    // Not atomic as a pair: a client built between the two renames fails one handshake. If the
    // second rename fails, the next sync or refresh finds the key doesn't match and rewrites both.
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
