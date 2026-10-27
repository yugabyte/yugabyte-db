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
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.gflags.SpecificGFlags.PerProcessFlags;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import io.fabric8.kubernetes.api.model.Secret;
import io.fabric8.kubernetes.api.model.SecretBuilder;
import java.io.File;
import java.math.BigInteger;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.attribute.PosixFilePermissions;
import java.security.KeyPair;
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
    caCertFile = TestHelper.createTempFile(certDir, "ca.root.crt", "ca-cert");
    clientDir = Paths.get(certDir, CertManagerClientCertSync.CLIENT_CERT_SUBDIR);
    UUID rootCA = createCA(CertConfigType.K8SCertManager, caCertFile);

    universe = ModelFactory.createUniverse("demo-universe", customer.getId());
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            ApiUtils.mockUniverseUpdater(userIntent, "demo-prefix", true /* setMasters */));
    universe = setDetails(d -> d.rootCA = rootCA);

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

  private static Pair<String, String> newCertPem(Duration validFor) throws Exception {
    return newCertPem(Instant.now().plus(validFor));
  }

  private static Pair<String, String> newCertPem(Instant notAfter) throws Exception {
    KeyPair keyPair = CertificateHelper.getKeyPairObject();
    X500Name subject = new X500Name("CN=yugabyte");
    Instant now = Instant.now();
    X509Certificate cert =
        new JcaX509CertificateConverter()
            .getCertificate(
                new JcaX509v3CertificateBuilder(
                        subject,
                        BigInteger.valueOf(now.toEpochMilli()),
                        Date.from(now.minus(Duration.ofDays(100))),
                        Date.from(notAfter),
                        subject,
                        keyPair.getPublic())
                    .build(
                        new JcaContentSignerBuilder(CertificateHelper.SIGNATURE_ALGO)
                            .build(keyPair.getPrivate())));
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
    Pair<String, String> files = CertManagerClientCertSync.getClientCertFiles(caCertFile).get();
    assertEquals(
        clientDir.resolve(CertificateHelper.CLIENT_CERT).toAbsolutePath().toString(),
        files.getLeft());
    assertEquals(
        clientDir.resolve(CertificateHelper.CLIENT_KEY).toAbsolutePath().toString(),
        files.getRight());
  }

  @Test
  public void testClientCertFilesAbsent() throws Exception {
    assertTrue(CertManagerClientCertSync.getClientCertFiles(caCertFile).isEmpty());
    assertTrue(CertManagerClientCertSync.getClientCertFiles(null).isEmpty());
    // A cert without its key is not usable.
    Files.createDirectories(clientDir);
    Files.writeString(clientDir.resolve(CertificateHelper.CLIENT_CERT), "cert");
    assertTrue(CertManagerClientCertSync.getClientCertFiles(caCertFile).isEmpty());
  }

  @Test
  public void testExpiredClientCertNotPresented() throws Exception {
    writeLocal(newCertPem(Instant.now().minus(Duration.ofDays(1))));
    assertTrue(CertManagerClientCertSync.getClientCertFiles(caCertFile).isEmpty());
  }

  @Test
  public void testVaultClientCertBesideCaIgnored() throws Exception {
    File caDir = new File(caCertFile).getParentFile();
    Files.writeString(caDir.toPath().resolve(CertificateHelper.CLIENT_CERT), "cert");
    Files.writeString(caDir.toPath().resolve(CertificateHelper.CLIENT_KEY), "key");
    assertTrue(CertManagerClientCertSync.getClientCertFiles(caCertFile).isEmpty());
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
    assertTrue(CertManagerClientCertSync.getClientCertFiles(caCertFile).isEmpty());
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
    assertTrue(CertManagerClientCertSync.getClientCertFiles(caCertFile).isPresent());
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
  public void testRefreshSkippedWhenNotRequired() throws Exception {
    dropFlag();

    certSync.refreshIfNeeded(universe);

    verify(mockKubernetesManager, never()).getSecret(any(), anyString(), any());
  }

  @Test
  public void testRefreshRemovesUnusedCopy() throws Exception {
    writeLocal(newCertPem(Duration.ofDays(90)));
    dropFlag();

    certSync.refreshIfNeeded(universe);

    assertFalse(Files.exists(clientDir));
  }

  @Test
  public void testRefreshKeepsCopyUsedByAnotherUniverse() throws Exception {
    writeLocal(newCertPem(Duration.ofDays(90)));
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    Universe other = ModelFactory.createUniverse("other-universe", customer.getId());
    Universe.saveDetails(
        other.getUniverseUUID(),
        u -> {
          UniverseDefinitionTaskParams d = u.getUniverseDetails();
          d.clusters = details.clusters;
          d.rootCA = details.rootCA;
          u.setUniverseDetails(d);
        });
    dropFlag();

    certSync.refreshIfNeeded(universe);

    assertTrue(CertManagerClientCertSync.getClientCertFiles(caCertFile).isPresent());
  }

  @Test
  public void testRefreshSwallowsErrors() throws Exception {
    when(mockKubernetesManager.getSecret(any(), anyString(), any()))
        .thenThrow(new RuntimeException("cluster unreachable"));
    certSync.refreshIfNeeded(universe);
    assertTrue(CertManagerClientCertSync.getClientCertFiles(caCertFile).isEmpty());
  }
}
