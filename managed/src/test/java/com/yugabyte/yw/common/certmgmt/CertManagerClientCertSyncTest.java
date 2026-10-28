// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.certmgmt;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.gflags.SpecificGFlags.PerProcessFlags;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.FileData;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import io.fabric8.kubernetes.api.model.Secret;
import io.fabric8.kubernetes.api.model.SecretBuilder;
import java.math.BigInteger;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.attribute.PosixFilePermissions;
import java.security.KeyPair;
import java.security.KeyPairGenerator;
import java.security.cert.X509Certificate;
import java.time.Duration;
import java.time.Instant;
import java.util.Base64;
import java.util.Date;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import org.apache.commons.lang3.tuple.Pair;
import org.bouncycastle.asn1.x500.X500Name;
import org.bouncycastle.cert.jcajce.JcaX509CertificateConverter;
import org.bouncycastle.cert.jcajce.JcaX509v3CertificateBuilder;
import org.bouncycastle.operator.jcajce.JcaContentSignerBuilder;
import org.junit.Before;
import org.junit.Test;

public class CertManagerClientCertSyncTest extends FakeDBApplication {

  private Customer customer;
  private Provider provider;
  private AvailabilityZone az;
  private Universe universe;
  private String caCertFile;
  private Path clientDir;
  private UUID rootCA;
  private CertManagerClientCertSync certSync;

  @Before
  public void setUp() throws Exception {
    customer = ModelFactory.testCustomer();
    provider = ModelFactory.kubernetesProvider(customer);
    Map<String, String> config = new HashMap<>();
    config.put("KUBECONFIG", "test-kc");
    provider.setConfigMap(config);
    provider.save();
    Region region = Region.create(provider, "region-1", "Region 1", "default-image");
    az = AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    InstanceType instanceType =
        InstanceType.upsert(
            provider.getUuid(), "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());
    UserIntent userIntent = ApiUtils.getTestUserIntent(region, provider, instanceType, 3);
    userIntent.replicationFactor = 3;
    userIntent.universeName = "demo-universe";
    userIntent.enableNodeToNodeEncrypt = true;
    userIntent.masterGFlags = new HashMap<>();
    userIntent.tserverGFlags =
        new HashMap<>(Map.of(CertManagerClientCertSync.USE_CLIENT_CERTS_GFLAG, "true"));

    String certDir = TestHelper.TMP_PATH + "/" + UUID.randomUUID();
    caCertFile =
        TestHelper.createTempFile(
            certDir, "ca.root.crt", CertificateHelper.getAsPemString(ROOT_CA.getRight()));
    rootCA = createCA(CertConfigType.K8SCertManager, caCertFile);

    universe = ModelFactory.createUniverse("demo-universe", customer.getId());
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            ApiUtils.mockUniverseUpdater(userIntent, "demo-prefix", true /* setMasters */));
    universe = setDetails(d -> d.rootCA = rootCA);
    clientDir = CertManagerClientCertSync.getClientCertDir(universe.getUniverseUUID());

    certSync = app.injector().instanceOf(CertManagerClientCertSync.class);
  }

  private interface DetailsUpdater {
    void update(UniverseDefinitionTaskParams details);
  }

  private Universe setDetails(DetailsUpdater updater) {
    return Universe.saveDetails(
        universe.getUniverseUUID(),
        u -> {
          UniverseDefinitionTaskParams details = u.getUniverseDetails();
          updater.update(details);
          u.setUniverseDetails(details);
        });
  }

  private UUID createCA(CertConfigType type, String certFile) throws Exception {
    UUID uuid = UUID.randomUUID();
    CertificateInfo.create(
        uuid, customer.getUuid(), "ca-" + uuid, new Date(), new Date(), "", certFile, type);
    return uuid;
  }

  /** A CA's key and cert; ROOT_CA is the universe's root CA unless a test says otherwise. */
  public static final Pair<KeyPair, X509Certificate> ROOT_CA = newCA("test-root-ca");

  private static Pair<KeyPair, X509Certificate> newCA(String cn) {
    return newCA(cn, null);
  }

  /** A CA signed by issuer, or self-signed when issuer is null. */
  private static Pair<KeyPair, X509Certificate> newCA(
      String cn, Pair<KeyPair, X509Certificate> issuer) {
    try {
      KeyPair keyPair = CertificateHelper.getKeyPairObject();
      X500Name name = new X500Name("CN=" + cn);
      Instant now = Instant.now();
      X509Certificate cert =
          new JcaX509CertificateConverter()
              .getCertificate(
                  new JcaX509v3CertificateBuilder(
                          issuer == null
                              ? name
                              : new X500Name(issuer.getRight().getSubjectX500Principal().getName()),
                          BigInteger.valueOf(now.toEpochMilli()),
                          Date.from(now.minus(Duration.ofDays(1))),
                          Date.from(now.plus(Duration.ofDays(365))),
                          name,
                          keyPair.getPublic())
                      .build(
                          new JcaContentSignerBuilder(CertificateHelper.SIGNATURE_ALGO)
                              .build((issuer == null ? keyPair : issuer.getLeft()).getPrivate())));
      return Pair.of(keyPair, cert);
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  /** A client cert issued by ROOT_CA. */
  public static Pair<String, String> newCertPem(Duration validFor) throws Exception {
    return newCertPem(Instant.now().plus(validFor), ROOT_CA);
  }

  private static Pair<String, String> newCertPem(Instant notAfter) throws Exception {
    return newCertPem(notAfter, ROOT_CA);
  }

  private static Pair<String, String> newCertPem(
      Instant notAfter, Pair<KeyPair, X509Certificate> ca) throws Exception {
    KeyPair keyPair = CertificateHelper.getKeyPairObject();
    Instant now = Instant.now();
    X509Certificate cert =
        new JcaX509CertificateConverter()
            .getCertificate(
                new JcaX509v3CertificateBuilder(
                        new X500Name(ca.getRight().getSubjectX500Principal().getName()),
                        BigInteger.valueOf(now.toEpochMilli()),
                        Date.from(now.minus(Duration.ofDays(100))),
                        Date.from(notAfter),
                        new X500Name("CN=yugabyte"),
                        keyPair.getPublic())
                    .build(
                        new JcaContentSignerBuilder(CertificateHelper.SIGNATURE_ALGO)
                            .build(ca.getLeft().getPrivate())));
    return Pair.of(
        CertificateHelper.getAsPemString(cert),
        CertificateHelper.getAsPemString(keyPair.getPrivate()));
  }

  private static Secret secret(Pair<String, String> pem) {
    Base64.Encoder encoder = Base64.getEncoder();
    return new SecretBuilder()
        .addToData("tls.crt", encoder.encodeToString(pem.getLeft().getBytes()))
        .addToData("tls.key", encoder.encodeToString(pem.getRight().getBytes()))
        .build();
  }

  private void writeLocal(Pair<String, String> pem) throws Exception {
    Files.createDirectories(clientDir);
    Files.writeString(clientDir.resolve(CertificateHelper.CLIENT_CERT), pem.getLeft());
    Files.writeString(clientDir.resolve(CertificateHelper.CLIENT_KEY), pem.getRight());
  }

  private static RuntimeException secretNotFound() {
    return new RuntimeException(
        "Unable to get secret. Code: 1. Output: Error from server (NotFound): secrets"
            + " \"x-client-tls\" not found");
  }

  private void dropFlag() {
    universe =
        setDetails(
            d ->
                d.getPrimaryCluster()
                    .userIntent
                    .tserverGFlags
                    .remove(CertManagerClientCertSync.USE_CLIENT_CERTS_GFLAG));
  }

  private String localCert() throws Exception {
    return Files.readString(clientDir.resolve(CertificateHelper.CLIENT_CERT));
  }

  @Test
  public void testClientCertFilesPresent() throws Exception {
    writeLocal(newCertPem(Duration.ofDays(90)));
    Pair<String, String> files = CertManagerClientCertSync.getClientCertFiles(universe).get();
    assertEquals(
        clientDir.resolve(CertificateHelper.CLIENT_CERT).toAbsolutePath().toString(),
        files.getLeft());
    assertEquals(
        clientDir.resolve(CertificateHelper.CLIENT_KEY).toAbsolutePath().toString(),
        files.getRight());
  }

  @Test
  public void testClientCertFilesAbsent() throws Exception {
    assertTrue(CertManagerClientCertSync.getClientCertFiles(universe).isEmpty());
    // A cert without its key is not usable.
    Files.createDirectories(clientDir);
    Files.writeString(clientDir.resolve(CertificateHelper.CLIENT_CERT), "cert");
    assertTrue(CertManagerClientCertSync.getClientCertFiles(universe).isEmpty());
  }

  @Test
  public void testExpiredClientCertNotPresented() throws Exception {
    writeLocal(newCertPem(Instant.now().minus(Duration.ofDays(1))));
    assertTrue(CertManagerClientCertSync.getClientCertFiles(universe).isEmpty());
  }

  @Test
  public void testClientCertRequired() throws Exception {
    assertTrue(CertManagerClientCertSync.isClientCertRequired(universe));

    universe =
        setDetails(
            d ->
                d.getPrimaryCluster()
                    .userIntent
                    .tserverGFlags
                    .remove(CertManagerClientCertSync.USE_CLIENT_CERTS_GFLAG));
    assertFalse(CertManagerClientCertSync.isClientCertRequired(universe));

    universe =
        setDetails(
            d ->
                d.getPrimaryCluster()
                    .userIntent
                    .masterGFlags
                    .put(CertManagerClientCertSync.USE_CLIENT_CERTS_GFLAG, "true"));
    assertTrue(CertManagerClientCertSync.isClientCertRequired(universe));

    UUID selfSigned = createCA(CertConfigType.SelfSigned, caCertFile);
    universe = setDetails(d -> d.rootCA = selfSigned);
    assertFalse(CertManagerClientCertSync.isClientCertRequired(universe));
  }

  @Test
  public void testClientCertRequiredByAzGFlags() {
    dropFlag();
    universe =
        setDetails(
            d -> {
              UserIntent intent = d.getPrimaryCluster().userIntent;
              intent.specificGFlags = SpecificGFlags.construct(Map.of(), Map.of());
              PerProcessFlags azFlags = new PerProcessFlags();
              azFlags.value =
                  Map.of(
                      ServerType.TSERVER,
                      Map.of(CertManagerClientCertSync.USE_CLIENT_CERTS_GFLAG, "true"));
              intent.specificGFlags.setPerAZ(new HashMap<>(Map.of(az.getUuid(), azFlags)));
            });
    assertTrue(CertManagerClientCertSync.isClientCertRequired(universe));
  }

  @Test
  public void testSecretName() {
    universe = setDetails(d -> d.useNewHelmNamingStyle = false);
    assertEquals(
        "yugabyte-tls-client-cert",
        CertManagerClientCertSync.getSecretName(universe, az.getUuid()));

    universe = setDetails(d -> d.useNewHelmNamingStyle = true);
    String secretName = CertManagerClientCertSync.getSecretName(universe, az.getUuid());
    assertTrue(secretName, secretName.matches("ybdemo-universe-[a-z0-9]+-client-tls"));
  }

  @Test
  public void testSyncWritesPair() throws Exception {
    Pair<String, String> pem = newCertPem(Duration.ofDays(90));
    when(mockKubernetesManager.getSecret(any(), anyString(), any())).thenReturn(secret(pem));

    certSync.sync(universe, Duration.ZERO);

    assertEquals(pem.getLeft(), localCert());
    Path key = clientDir.resolve(CertificateHelper.CLIENT_KEY);
    assertEquals(pem.getRight(), Files.readString(key));
    assertEquals("rw-------", PosixFilePermissions.toString(Files.getPosixFilePermissions(key)));
  }

  @Test
  public void testSyncTimesOutWithoutSecret() throws Exception {
    when(mockKubernetesManager.getSecret(any(), anyString(), any())).thenThrow(secretNotFound());
    RuntimeException e =
        assertThrows(RuntimeException.class, () -> certSync.sync(universe, Duration.ZERO));
    assertTrue(e.getMessage(), e.getMessage().startsWith("Timed out"));
    assertTrue(e.getCause() == null);
    assertTrue(CertManagerClientCertSync.getClientCertFiles(universe).isEmpty());
  }

  @Test
  public void testSyncTimeoutReportsReadError() throws Exception {
    when(mockKubernetesManager.getSecret(any(), anyString(), any()))
        .thenThrow(new RuntimeException("Forbidden"));
    RuntimeException e =
        assertThrows(RuntimeException.class, () -> certSync.sync(universe, Duration.ZERO));
    assertTrue(e.getMessage(), e.getMessage().endsWith(": Forbidden"));
    assertEquals("Forbidden", e.getCause().getMessage());
  }

  @Test
  public void testSyncWaitsForSecretFromRootCA() throws Exception {
    // Step 3 of a root CA rotation, with the Secret not yet reissued from the new CA.
    Pair<String, String> local = newCertPem(Duration.ofDays(90));
    writeLocal(local);
    when(mockKubernetesManager.getSecret(any(), anyString(), any()))
        .thenReturn(secret(newCertPem(Instant.now().plus(Duration.ofDays(90)), newCA("old-ca"))));

    RuntimeException e =
        assertThrows(RuntimeException.class, () -> certSync.sync(universe, Duration.ZERO));

    assertTrue(e.getMessage(), e.getMessage().contains("not issued by root CA " + rootCA));
    assertEquals(local.getLeft(), localCert());
  }

  @Test
  public void testSyncRewritesMismatchedKey() throws Exception {
    Pair<String, String> pem = newCertPem(Duration.ofDays(90));
    writeLocal(Pair.of(pem.getLeft(), "stale-key"));
    when(mockKubernetesManager.getSecret(any(), anyString(), any())).thenReturn(secret(pem));

    certSync.sync(universe, Duration.ZERO);

    assertEquals(pem.getRight(), Files.readString(clientDir.resolve(CertificateHelper.CLIENT_KEY)));
  }

  @Test
  public void testRefreshRestoresMissingKey() throws Exception {
    Pair<String, String> pem = newCertPem(Duration.ofDays(90));
    writeLocal(pem);
    Files.delete(clientDir.resolve(CertificateHelper.CLIENT_KEY));
    when(mockKubernetesManager.getSecret(any(), anyString(), any())).thenReturn(secret(pem));

    certSync.refreshIfNeeded(universe);

    assertEquals(pem.getRight(), Files.readString(clientDir.resolve(CertificateHelper.CLIENT_KEY)));
    assertTrue(CertManagerClientCertSync.getClientCertFiles(universe).isPresent());
  }

  @Test
  public void testRefreshSkipsValidLocalCert() throws Exception {
    Pair<String, String> local = newCertPem(Duration.ofDays(60));
    writeLocal(local);

    certSync.refreshIfNeeded(universe);

    verify(mockKubernetesManager, never()).getSecret(any(), anyString(), any());
    assertEquals(local.getLeft(), localCert());
  }

  @Test
  public void testRefreshReplacesExpiringCert() throws Exception {
    writeLocal(newCertPem(Duration.ofDays(10)));
    Pair<String, String> renewed = newCertPem(Duration.ofDays(90));
    when(mockKubernetesManager.getSecret(any(), anyString(), eq("demo-prefix")))
        .thenReturn(secret(renewed));

    certSync.refreshIfNeeded(universe);

    assertEquals(renewed.getLeft(), localCert());
  }

  @Test
  public void testRefreshSkipsDeletedUniverse() throws Exception {
    // A queued health check still holds the universe after it is deleted, and the Secret outlives
    // it, e.g. on a detach.
    Universe snapshot = universe;
    Universe.delete(universe.getUniverseUUID());
    when(mockKubernetesManager.getSecret(any(), anyString(), any()))
        .thenReturn(secret(newCertPem(Duration.ofDays(90))));

    certSync.refreshIfNeeded(snapshot);

    verify(mockKubernetesManager, never()).getSecret(any(), anyString(), any());
    assertFalse(Files.exists(clientDir));
  }

  @Test
  public void testRefreshKeepsCopyWhenSecretIsFromAnotherCA() throws Exception {
    // The issuer moved to a new CA ahead of the rotation, and the Secret was renewed from it.
    Pair<String, String> local = newCertPem(Duration.ofDays(10));
    writeLocal(local);
    when(mockKubernetesManager.getSecret(any(), anyString(), any()))
        .thenReturn(secret(newCertPem(Instant.now().plus(Duration.ofDays(90)), newCA("next-ca"))));

    certSync.refreshIfNeeded(universe);

    assertEquals(local.getLeft(), localCert());
  }

  @Test
  public void testRefreshCopiesNothingFromAnotherCA() throws Exception {
    when(mockKubernetesManager.getSecret(any(), anyString(), any()))
        .thenReturn(secret(newCertPem(Instant.now().plus(Duration.ofDays(90)), newCA("next-ca"))));

    certSync.refreshIfNeeded(universe);

    assertFalse(Files.exists(clientDir.resolve(CertificateHelper.CLIENT_CERT)));
  }

  @Test
  public void testRefreshAcceptsEitherCAOfMultiCertRoot() throws Exception {
    // Mid-rotation the root CA file holds the old and the new CA.
    Pair<KeyPair, X509Certificate> next = newCA("next-ca");
    Files.writeString(
        Paths.get(caCertFile),
        CertificateHelper.getAsPemString(ROOT_CA.getRight())
            + CertificateHelper.getAsPemString(next.getRight()));
    Pair<String, String> pem = newCertPem(Instant.now().plus(Duration.ofDays(90)), next);
    when(mockKubernetesManager.getSecret(any(), anyString(), any())).thenReturn(secret(pem));

    certSync.refreshIfNeeded(universe);

    assertEquals(pem.getLeft(), localCert());
  }

  @Test
  public void testIssuedByRootCAFollowsIntermediate() throws Exception {
    Pair<KeyPair, X509Certificate> intermediate = newCA("intermediate-ca", ROOT_CA);
    Pair<String, String> leaf = newCertPem(Instant.now().plus(Duration.ofDays(90)), intermediate);
    String chain = leaf.getLeft() + CertificateHelper.getAsPemString(intermediate.getRight());

    assertTrue(CertManagerClientCertSync.isIssuedByRootCA(chain, rootCA));
    assertFalse(CertManagerClientCertSync.isIssuedByRootCA(leaf.getLeft(), rootCA));
  }

  @Test
  public void testCopyPersistedInDBAndRemovedOnDelete() throws Exception {
    when(mockKubernetesManager.getSecret(any(), anyString(), any()))
        .thenReturn(secret(newCertPem(Duration.ofDays(90))));
    certSync.sync(universe, Duration.ZERO);
    String certPath = clientDir.resolve(CertificateHelper.CLIENT_CERT).toString();
    String keyPath = clientDir.resolve(CertificateHelper.CLIENT_KEY).toString();
    assertTrue(FileData.getFromFile(FileData.getRelativePath(certPath)) != null);
    assertTrue(FileData.getFromFile(FileData.getRelativePath(keyPath)) != null);

    CertManagerClientCertSync.deleteClientCert(universe.getUniverseUUID());

    assertTrue(FileData.getFromFile(FileData.getRelativePath(certPath)) == null);
    assertTrue(FileData.getFromFile(FileData.getRelativePath(keyPath)) == null);
  }

  @Test
  public void testRefreshSkippedWhenNotRequired() throws Exception {
    dropFlag();

    certSync.refreshIfNeeded(universe);

    verify(mockKubernetesManager, never()).getSecret(any(), anyString(), any());
  }

  @Test
  public void testClientCertRequiredForNewRootCA() throws Exception {
    UUID selfSigned = createCA(CertConfigType.SelfSigned, caCertFile);
    universe = setDetails(d -> d.rootCA = selfSigned);
    assertFalse(CertManagerClientCertSync.isClientCertRequired(universe));
    assertTrue(CertManagerClientCertSync.isClientCertRequired(universe, rootCA));
  }

  @Test
  public void testRefreshReplacesMismatchedKey() throws Exception {
    Pair<String, String> pem = newCertPem(Duration.ofDays(60));
    writeLocal(Pair.of(pem.getLeft(), newCertPem(Duration.ofDays(60)).getRight()));
    when(mockKubernetesManager.getSecret(any(), anyString(), any())).thenReturn(secret(pem));

    certSync.refreshIfNeeded(universe);

    assertEquals(pem.getRight(), Files.readString(clientDir.resolve(CertificateHelper.CLIENT_KEY)));
  }

  @Test
  public void testValidateHelmOverridesRejectsNonRsaKey() {
    String ecdsa = "tls:\n  certManager:\n    certificates:\n      algorithm: ECDSA\n";
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    PlatformServiceException e =
        assertThrows(
            PlatformServiceException.class,
            () -> CertManagerClientCertSync.validateHelmOverrides(details, ecdsa, null));
    assertTrue(e.getMessage(), e.getMessage().contains("algorithm=ECDSA"));
    assertThrows(
        PlatformServiceException.class,
        () -> CertManagerClientCertSync.validateHelmOverrides(details, "", Map.of("az-1", ecdsa)));

    String rsa = "tls:\n  certManager:\n    certificates:\n      algorithm: RSA\n";
    CertManagerClientCertSync.validateHelmOverrides(details, rsa, null);

    dropFlag();
    CertManagerClientCertSync.validateHelmOverrides(universe.getUniverseDetails(), ecdsa, null);
  }

  @Test
  public void testSyncRejectsNonRsaCert() throws Exception {
    KeyPairGenerator generator = KeyPairGenerator.getInstance("EC");
    generator.initialize(256);
    KeyPair keyPair = generator.generateKeyPair();
    X500Name subject = new X500Name("CN=yugabyte");
    Instant now = Instant.now();
    X509Certificate cert =
        new JcaX509CertificateConverter()
            .getCertificate(
                new JcaX509v3CertificateBuilder(
                        subject,
                        BigInteger.ONE,
                        Date.from(now.minus(Duration.ofDays(1))),
                        Date.from(now.plus(Duration.ofDays(90))),
                        subject,
                        keyPair.getPublic())
                    .build(
                        new JcaContentSignerBuilder("SHA256withECDSA")
                            .build(keyPair.getPrivate())));
    Pair<String, String> pem =
        Pair.of(
            CertificateHelper.getAsPemString(cert),
            CertificateHelper.getAsPemString(keyPair.getPrivate()));
    when(mockKubernetesManager.getSecret(any(), anyString(), any())).thenReturn(secret(pem));

    PlatformServiceException e =
        assertThrows(PlatformServiceException.class, () -> certSync.sync(universe, Duration.ZERO));
    assertTrue(e.getMessage(), e.getMessage().contains("EC key"));
    assertFalse(Files.exists(clientDir));
  }

  @Test
  public void testNotPresentedWhenNotRequired() throws Exception {
    writeLocal(newCertPem(Duration.ofDays(90)));
    dropFlag();
    assertTrue(CertManagerClientCertSync.getClientCertFiles(universe).isEmpty());
  }

  @Test
  public void testNotPresentedToAnotherUniverseOnSameCA() throws Exception {
    writeLocal(newCertPem(Duration.ofDays(90)));
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    Universe other = ModelFactory.createUniverse("other-universe", customer.getId());
    other =
        Universe.saveDetails(
            other.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams d = u.getUniverseDetails();
              d.clusters = details.clusters;
              d.rootCA = details.rootCA;
              u.setUniverseDetails(d);
            });
    assertTrue(CertManagerClientCertSync.isClientCertRequired(other));
    assertTrue(CertManagerClientCertSync.getClientCertFiles(other).isEmpty());
  }

  @Test
  public void testCADirectoryUntouched() throws Exception {
    when(mockKubernetesManager.getSecret(any(), anyString(), any()))
        .thenReturn(secret(newCertPem(Duration.ofDays(90))));
    certSync.sync(universe, Duration.ZERO);
    try (var entries = Files.list(Paths.get(caCertFile).getParent())) {
      assertEquals(1, entries.count());
    }
  }

  @Test
  public void testDeleteClientCert() throws Exception {
    writeLocal(newCertPem(Duration.ofDays(90)));
    CertManagerClientCertSync.deleteClientCert(universe.getUniverseUUID());
    assertFalse(Files.exists(clientDir));
  }

  @Test
  public void testRefreshRemovesUnusedCopy() throws Exception {
    writeLocal(newCertPem(Duration.ofDays(90)));
    dropFlag();

    certSync.refreshIfNeeded(universe);

    assertFalse(Files.exists(clientDir));
  }

  @Test
  public void testRefreshSwallowsErrors() throws Exception {
    when(mockKubernetesManager.getSecret(any(), anyString(), any()))
        .thenThrow(new RuntimeException("cluster unreachable"));
    certSync.refreshIfNeeded(universe);
    assertTrue(CertManagerClientCertSync.getClientCertFiles(universe).isEmpty());
  }
}
