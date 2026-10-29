// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.certmgmt;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.FileData;
import com.yugabyte.yw.models.Universe;
import java.io.IOException;
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
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import javax.annotation.Nullable;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.tuple.Pair;

/**
 * The client cert YBA presents on RPCs to a universe whose servers require one. A universe opts in
 * at creation with userIntent.useNodeToNodeClientCerts, and KubernetesCommandExecutor sets
 * node_to_node_encryption_use_client_certificates from it. Servers only check that the cert chains
 * to their root CA, so one client cert per CA serves every universe on it.
 *
 * <p>The cert and key sit in the CA's directory, as CertificateHelper.getClientCertFile names them,
 * and in the DB like the CA itself. They are loaded when a client is built: from disk, else from
 * the DB (a replaced volume), else from the CA's issuer (missing everywhere, or close to expiry).
 */
@Slf4j
@Singleton
public class RpcClientCert {

  // AsyncYBClient loads the client key with KeyFactory.getInstance("RSA").
  private static final String SUPPORTED_KEY_ALGORITHM = "RSA";
  // Under cert-manager's default renewBefore of 15 days, so the issuer has renewed by then.
  @VisibleForTesting static final Duration REFETCH_BEFORE_EXPIRY = Duration.ofDays(10);
  // While the current cert is still valid, how often to ask the issuer for a renewed one.
  @VisibleForTesting static final Duration REFETCH_INTERVAL = Duration.ofMinutes(5);

  private final CertManagerClientCert certManagerClientCert;
  // Keyed by the client cert file, so universes on one CA load it once.
  private final Map<String, Object> locks = new ConcurrentHashMap<>();
  private final Map<String, Instant> lastFetch = new ConcurrentHashMap<>();

  @Inject
  public RpcClientCert(CertManagerClientCert certManagerClientCert) {
    this.certManagerClientCert = certManagerClientCert;
  }

  public static boolean isClientCertRequired(Universe universe) {
    return isClientCertRequired(universe.getUniverseDetails());
  }

  public static boolean isClientCertRequired(UniverseDefinitionTaskParams details) {
    return details.getPrimaryCluster().userIntent.useNodeToNodeClientCerts;
  }

  /** Rejects turning on client cert auth for a universe YBA can't get a client cert for. */
  public static void validate(UniverseDefinitionTaskParams details) {
    UserIntent intent = details.getPrimaryCluster().userIntent;
    if (!intent.useNodeToNodeClientCerts) {
      return;
    }
    if (!intent.getAllCloudTypes().stream().allMatch(t -> t == CloudType.kubernetes)) {
      throw invalid("is only supported on Kubernetes universes");
    }
    if (!intent.enableNodeToNodeEncrypt) {
      throw invalid("requires node-to-node encryption");
    }
    validateRootCA(details.rootCA);
  }

  /** Rejects a root CA YBA can't get a client cert for. */
  public static void validateRootCA(@Nullable UUID rootCA) {
    boolean supported =
        rootCA != null
            && CertificateInfo.maybeGet(rootCA)
                .map(c -> isSupported(c.getCertType()))
                .orElse(false);
    if (!supported) {
      throw invalid("requires a K8SCertManager root CA");
    }
  }

  private static PlatformServiceException invalid(String reason) {
    return new PlatformServiceException(BAD_REQUEST, "useNodeToNodeClientCerts " + reason);
  }

  // The CA types YBA can get a client cert for; see fetch.
  private static boolean isSupported(CertConfigType certType) {
    return certType == CertConfigType.K8SCertManager;
  }

  /**
   * The cert and key files to present to universe, if it requires them and a valid pair can be
   * loaded. Never throws: a client built without the cert fails its RPCs on the server's refusal.
   */
  public Optional<Pair<String, String>> getFiles(Universe universe) {
    if (!isClientCertRequired(universe)) {
      return Optional.empty();
    }
    try {
      return Optional.of(load(universe));
    } catch (Exception e) {
      log.warn("No client cert for universe {}: {}", universe.getName(), e.getMessage());
      return Optional.empty();
    }
  }

  /** The cert and key files for universe; throws why there is no valid pair. */
  public Pair<String, String> load(Universe universe) {
    UUID ca = getOwningCA(universe.getUniverseDetails().rootCA);
    Pair<String, String> files =
        Pair.of(CertificateHelper.getClientCertFile(ca), CertificateHelper.getClientKeyFile(ca));
    if (isUsable(files, REFETCH_BEFORE_EXPIRY)) {
      return files;
    }
    synchronized (locks.computeIfAbsent(files.getLeft(), f -> new Object())) {
      try {
        refresh(universe, ca, files);
      } catch (Exception e) {
        if (!isUsable(files, Duration.ZERO)) {
          throw new IllegalStateException(e.getMessage(), e);
        }
        log.warn("Keeping client cert {}, which expires soon: {}", files.getLeft(), e.getMessage());
      }
    }
    return files;
  }

  // Disk, then DB, then the issuer. Runs under the file's lock.
  private void refresh(Universe universe, UUID ca, Pair<String, String> files) throws Exception {
    // Loaded by another client build while this one waited for the lock.
    if (isUsable(files, REFETCH_BEFORE_EXPIRY)) {
      return;
    }
    if (!isUsable(files, Duration.ZERO)) {
      restoreFromDB(files);
      if (isUsable(files, REFETCH_BEFORE_EXPIRY)) {
        return;
      }
    }
    Instant last = lastFetch.get(files.getLeft());
    if (isUsable(files, Duration.ZERO)
        && last != null
        && last.plus(REFETCH_INTERVAL).isAfter(Instant.now())) {
      return;
    }
    lastFetch.put(files.getLeft(), Instant.now());
    Pair<String, String> pem = fetch(universe, ca);
    // A cert-manager issuer moved ahead of a root CA rotation signs with a CA the servers don't
    // trust yet.
    if (!isIssuedBy(pem.getLeft(), ca)) {
      throw new IllegalStateException(
          "The issuer's client cert for universe "
              + universe.getName()
              + " is not signed by its root CA "
              + ca);
    }
    String algorithm =
        CertificateHelper.convertStringToX509Cert(pem.getLeft()).getPublicKey().getAlgorithm();
    if (!SUPPORTED_KEY_ALGORITHM.equals(algorithm)) {
      throw new IllegalStateException(
          String.format(
              "The client cert for universe %s has a %s key; YugabyteDB Anywhere can only present"
                  + " an %s client certificate",
              universe.getName(), algorithm, SUPPORTED_KEY_ALGORITHM));
    }
    write(files, pem);
  }

  private Pair<String, String> fetch(Universe universe, UUID ca) {
    CertConfigType certType = CertificateInfo.getOrBadRequest(ca).getCertType();
    switch (certType) {
      case K8SCertManager:
        return certManagerClientCert.read(universe);
      default:
        throw new IllegalStateException("No client cert source for a " + certType + " CA");
    }
  }

  /**
   * The CA whose client cert to present. During a root CA rotation the universe uses a temporary
   * multi-CA cert until Reset; YBA keeps presenting the old CA's client cert, which every server
   * trusts until then.
   */
  @VisibleForTesting
  static UUID getOwningCA(UUID rootCA) {
    CertificateInfo info = CertificateInfo.getOrBadRequest(rootCA);
    if (!CertificateInfo.isTemporary(info)) {
      return rootCA;
    }
    // <old CA>.<universe>.ca.multi.root.crt, as UniverseUpdateRootCert names it.
    String fileName = Paths.get(info.getCertificate()).getFileName().toString();
    return UUID.fromString(fileName.split("\\.")[0]);
  }

  // Whether certPem (a leaf, optionally followed by its intermediates) chains to ca.
  @VisibleForTesting
  static boolean isIssuedBy(String certPem, UUID ca) throws Exception {
    List<X509Certificate> trusted =
        CertificateHelper.convertStringToX509CertList(
            Files.readString(Paths.get(CertificateInfo.getOrBadRequest(ca).getCertificate())));
    List<X509Certificate> chain = CertificateHelper.convertStringToX509CertList(certPem);
    X509Certificate cert = chain.get(0);
    // Bounded by the chain length, so a cycle in a malformed bundle can't loop.
    for (int i = 0; i <= chain.size(); i++) {
      X509Certificate current = cert;
      if (trusted.stream().anyMatch(t -> isSignedBy(current, t))) {
        return true;
      }
      Optional<X509Certificate> issuer =
          chain.stream().filter(c -> c != current && isSignedBy(current, c)).findFirst();
      if (issuer.isEmpty()) {
        return false;
      }
      cert = issuer.get();
    }
    return false;
  }

  private static boolean isSignedBy(X509Certificate cert, X509Certificate ca) {
    try {
      cert.verify(ca.getPublicKey());
      return true;
    } catch (Exception e) {
      return false;
    }
  }

  // Present, the key matches the cert, and the cert is valid until margin from now.
  private static boolean isUsable(Pair<String, String> files, Duration margin) {
    try {
      if (!Files.isRegularFile(Paths.get(files.getLeft()))
          || !Files.isRegularFile(Paths.get(files.getRight()))) {
        return false;
      }
      X509Certificate cert = CertificateHelper.getCertificateFromFile(files.getLeft());
      Instant now = Instant.now();
      return !now.isBefore(cert.getNotBefore().toInstant())
          && !Date.from(now.plus(margin)).after(cert.getNotAfter())
          && CertificateHelper.verifySignature(cert, Files.readString(Paths.get(files.getRight())));
    } catch (Exception e) {
      log.warn("Unreadable client cert {}: {}", files.getLeft(), e.getMessage());
      return false;
    }
  }

  private static void restoreFromDB(Pair<String, String> files) throws IOException {
    Optional<String> cert = readFromDB(files.getLeft());
    Optional<String> key = readFromDB(files.getRight());
    if (cert.isPresent() && key.isPresent()) {
      replace(files.getRight(), key.get());
      replace(files.getLeft(), cert.get());
      log.info("Restored client cert {} from the DB", files.getLeft());
    }
  }

  private static Optional<String> readFromDB(String file) {
    return Optional.ofNullable(FileData.getFromFile(FileData.getRelativePath(file)))
        .map(data -> new String(Base64.getDecoder().decode(data.getFileContent())));
  }

  private static void write(Pair<String, String> files, Pair<String, String> pem)
      throws IOException {
    // Key first: a crash between the two leaves a pair isUsable rejects, not a stale key.
    replace(files.getRight(), pem.getRight());
    replace(files.getLeft(), pem.getLeft());
    FileData.upsertFileInDB(files.getRight());
    FileData.upsertFileInDB(files.getLeft());
    log.info("Stored client cert {}", files.getLeft());
  }

  private static void replace(String file, String content) throws IOException {
    Path path = Paths.get(file);
    Path tmp =
        Files.createTempFile(
            path.getParent(),
            path.getFileName().toString(),
            ".tmp",
            PosixFilePermissions.asFileAttribute(PosixFilePermissions.fromString("rw-------")));
    try {
      Files.writeString(tmp, content);
      Files.move(tmp, path, StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
    } finally {
      Files.deleteIfExists(tmp);
    }
  }
}
