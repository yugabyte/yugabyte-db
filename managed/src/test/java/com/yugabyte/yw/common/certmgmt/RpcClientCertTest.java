// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.certmgmt;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.AppConfigHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
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
import java.security.PublicKey;
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

public class RpcClientCertTest extends FakeDBApplication {

  /** A CA's key and cert; ROOT_CA is the universe's root CA unless a test says otherwise. */
  public static final Pair<KeyPair, X509Certificate> ROOT_CA = newCA("test-root-ca");

  private Customer customer;
  private AvailabilityZone az;
  private Universe universe;
  private UUID rootCA;
  private Path certFile;
  private Path keyFile;
  private RpcClientCert rpcClientCert;

  @Before
  public void setUp() throws Exception {
    customer = ModelFactory.testCustomer();
    Provider provider = ModelFactory.kubernetesProvider(customer);
    provider.setConfigMap(new HashMap<>(Map.of("KUBECONFIG", "test-kc")));
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
    userIntent.useNodeToNodeClientCerts = true;
    userIntent.providerType = CloudType.kubernetes;
    userIntent.masterGFlags = new HashMap<>();
    userIntent.tserverGFlags = new HashMap<>();

    rootCA = createCA(CertConfigType.K8SCertManager, ROOT_CA.getRight());
    universe = ModelFactory.createUniverse("demo-universe", customer.getId());
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            ApiUtils.mockUniverseUpdater(userIntent, "demo-prefix", true /* setMasters */));
    universe = setDetails(d -> d.rootCA = rootCA);
    certFile = Paths.get(CertificateHelper.getClientCertFile(rootCA));
    keyFile = Paths.get(CertificateHelper.getClientKeyFile(rootCA));

    rpcClientCert = app.injector().instanceOf(RpcClientCert.class);
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

  // Where YBA stores an uploaded CA.
  private UUID createCA(CertConfigType type, X509Certificate... certs) throws Exception {
    UUID uuid = UUID.randomUUID();
    Path caFile =
        Paths.get(
            CertificateHelper.getCACertPath(
                AppConfigHelper.getStoragePath(), customer.getUuid(), uuid));
    StringBuilder pem = new StringBuilder();
    for (X509Certificate cert : certs) {
      pem.append(CertificateHelper.getAsPemString(cert));
    }
    Files.createDirectories(caFile.getParent());
    Files.writeString(caFile, pem.toString());
    CertificateInfo.create(
        uuid,
        customer.getUuid(),
        "ca-" + uuid,
        new Date(),
        new Date(),
        "",
        caFile.toString(),
        type);
    return uuid;
  }

  private static Pair<KeyPair, X509Certificate> newCA(String cn) {
    return newCA(cn, null);
  }

  /** A CA signed by issuer, or self-signed when issuer is null. */
  private static Pair<KeyPair, X509Certificate> newCA(
      String cn, Pair<KeyPair, X509Certificate> issuer) {
    try {
      KeyPair keyPair = CertificateHelper.getKeyPairObject();
      X509Certificate cert =
          sign(
              new X500Name("CN=" + cn),
              keyPair.getPublic(),
              Instant.now().plus(Duration.ofDays(365)),
              issuer == null ? Pair.of(keyPair, null) : issuer);
      return Pair.of(keyPair, cert);
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  // A cert for subjectKey, signed by issuer's key; a null issuer cert means self-signed.
  private static X509Certificate sign(
      X500Name subject,
      PublicKey subjectKey,
      Instant notAfter,
      Pair<KeyPair, X509Certificate> issuer)
      throws Exception {
    Instant now = Instant.now();
    X500Name issuerName =
        issuer.getRight() == null
            ? subject
            : new X500Name(issuer.getRight().getSubjectX500Principal().getName());
    return new JcaX509CertificateConverter()
        .getCertificate(
            new JcaX509v3CertificateBuilder(
                    issuerName,
                    BigInteger.valueOf(System.nanoTime()),
                    Date.from(now.minus(Duration.ofDays(100))),
                    Date.from(notAfter),
                    subject,
                    subjectKey)
                .build(
                    new JcaContentSignerBuilder(CertificateHelper.SIGNATURE_ALGO)
                        .build(issuer.getLeft().getPrivate())));
  }

  /** A client cert issued by ROOT_CA. */
  public static Pair<String, String> newCertPem(Duration validFor) throws Exception {
    return newCertPem(Instant.now().plus(validFor), ROOT_CA);
  }

  private static Pair<String, String> newCertPem(
      Instant notAfter, Pair<KeyPair, X509Certificate> ca) throws Exception {
    KeyPair keyPair = CertificateHelper.getKeyPairObject();
    X509Certificate cert = sign(new X500Name("CN=yugabyte"), keyPair.getPublic(), notAfter, ca);
    return Pair.of(
        CertificateHelper.getAsPemString(cert),
        CertificateHelper.getAsPemString(keyPair.getPrivate()));
  }

  private static Pair<String, String> fromOtherCA() throws Exception {
    return newCertPem(Instant.now().plus(Duration.ofDays(90)), newCA("next-ca"));
  }

  public static Secret secret(Pair<String, String> pem) {
    Base64.Encoder encoder = Base64.getEncoder();
    return new SecretBuilder()
        .addToData("tls.crt", encoder.encodeToString(pem.getLeft().getBytes()))
        .addToData("tls.key", encoder.encodeToString(pem.getRight().getBytes()))
        .build();
  }

  private void givenSecret(Pair<String, String> pem) {
    when(mockKubernetesManager.getSecret(any(), anyString(), any())).thenReturn(secret(pem));
  }

  private void writeLocal(Pair<String, String> pem) throws Exception {
    Files.writeString(certFile, pem.getLeft());
    Files.writeString(keyFile, pem.getRight());
  }

  private String localCert() throws Exception {
    return Files.readString(certFile);
  }

  private void dropFlag() {
    universe = setDetails(d -> d.getPrimaryCluster().userIntent.useNodeToNodeClientCerts = false);
  }

  private void assertRejected(UniverseDefinitionTaskParams details, String message) {
    PlatformServiceException e =
        assertThrows(PlatformServiceException.class, () -> RpcClientCert.validate(details));
    assertTrue(e.getMessage(), e.getMessage().contains(message));
  }

  @Test
  public void testLoadsFromIssuerNextToCA() throws Exception {
    Pair<String, String> pem = newCertPem(Duration.ofDays(90));
    givenSecret(pem);

    Pair<String, String> files = rpcClientCert.getFiles(universe).get();

    assertEquals(Pair.of(certFile.toString(), keyFile.toString()), files);
    assertEquals(pem.getLeft(), localCert());
    assertEquals(pem.getRight(), Files.readString(keyFile));
    assertEquals(
        "rw-------", PosixFilePermissions.toString(Files.getPosixFilePermissions(keyFile)));
    assertNotNull(FileData.getFromFile(FileData.getRelativePath(certFile.toString())));
    assertNotNull(FileData.getFromFile(FileData.getRelativePath(keyFile.toString())));
  }

  @Test
  public void testUsesValidFileWithoutReadingIssuer() throws Exception {
    writeLocal(newCertPem(Duration.ofDays(60)));

    assertTrue(rpcClientCert.getFiles(universe).isPresent());

    verify(mockKubernetesManager, never()).getSecret(any(), anyString(), any());
  }

  @Test
  public void testRestoresDeletedFileFromDB() throws Exception {
    // The issuer has moved on to the next CA, so only the DB has a cert the servers trust.
    Pair<String, String> pem = newCertPem(Duration.ofDays(90));
    givenSecret(pem);
    rpcClientCert.load(universe);
    Files.delete(certFile);
    Files.delete(keyFile);
    givenSecret(fromOtherCA());

    rpcClientCert.load(universe);

    assertEquals(pem.getLeft(), localCert());
    assertEquals(pem.getRight(), Files.readString(keyFile));
  }

  @Test
  public void testRefetchesNearExpiry() throws Exception {
    writeLocal(newCertPem(Duration.ofDays(5)));
    Pair<String, String> renewed = newCertPem(Duration.ofDays(90));
    givenSecret(renewed);

    rpcClientCert.load(universe);

    assertEquals(renewed.getLeft(), localCert());
  }

  @Test
  public void testKeepsCertWhenIssuerSignsWithAnotherCA() throws Exception {
    // A cert-manager issuer moved to the next CA ahead of the root CA rotation.
    Pair<String, String> local = newCertPem(Duration.ofDays(5));
    writeLocal(local);
    givenSecret(fromOtherCA());

    assertTrue(rpcClientCert.getFiles(universe).isPresent());
    assertEquals(local.getLeft(), localCert());
  }

  @Test
  public void testTakesNothingFromAnotherCA() throws Exception {
    givenSecret(fromOtherCA());

    IllegalStateException e =
        assertThrows(IllegalStateException.class, () -> rpcClientCert.load(universe));

    assertTrue(e.getMessage(), e.getMessage().contains("not signed by its root CA"));
    assertFalse(Files.exists(certFile));
    assertTrue(rpcClientCert.getFiles(universe).isEmpty());
  }

  @Test
  public void testThrottlesIssuerWhileCertIsValid() throws Exception {
    Pair<String, String> local = newCertPem(Duration.ofDays(5));
    writeLocal(local);
    // Not renewed yet.
    givenSecret(local);

    rpcClientCert.load(universe);
    rpcClientCert.load(universe);

    verify(mockKubernetesManager, times(1)).getSecret(any(), anyString(), any());
  }

  @Test
  public void testRetriesIssuerWhileNoValidCert() throws Exception {
    when(mockKubernetesManager.getSecret(any(), anyString(), any())).thenReturn(null);

    assertThrows(IllegalStateException.class, () -> rpcClientCert.load(universe));
    assertThrows(IllegalStateException.class, () -> rpcClientCert.load(universe));

    verify(mockKubernetesManager, times(2)).getSecret(any(), anyString(), any());
  }

  @Test
  public void testMissingSecretReportsItsName() throws Exception {
    when(mockKubernetesManager.getSecret(any(), anyString(), any()))
        .thenThrow(new RuntimeException("Error from server (NotFound): secrets \"x\" not found"));

    IllegalStateException e =
        assertThrows(IllegalStateException.class, () -> rpcClientCert.load(universe));

    assertTrue(e.getMessage(), e.getMessage().contains("is not ready"));
  }

  @Test
  public void testReplacesMismatchedKey() throws Exception {
    Pair<String, String> pem = newCertPem(Duration.ofDays(60));
    writeLocal(Pair.of(pem.getLeft(), newCertPem(Duration.ofDays(60)).getRight()));
    givenSecret(pem);

    rpcClientCert.load(universe);

    assertEquals(pem.getRight(), Files.readString(keyFile));
  }

  @Test
  public void testExpiredCertNotPresented() throws Exception {
    writeLocal(newCertPem(Instant.now().minus(Duration.ofDays(1)), ROOT_CA));
    when(mockKubernetesManager.getSecret(any(), anyString(), any()))
        .thenThrow(new RuntimeException("cluster unreachable"));

    assertTrue(rpcClientCert.getFiles(universe).isEmpty());
  }

  @Test
  public void testRotationPresentsOldCAUntilReset() throws Exception {
    // Steps 1 to 2a of a root CA rotation: the universe uses a temporary multi-CA cert, written
    // into the old CA's directory, and the issuer already signs with the new CA.
    Pair<KeyPair, X509Certificate> next = newCA("next-ca");
    Path multiCert =
        certFile.resolveSibling(rootCA + "." + universe.getUniverseUUID() + ".ca.multi.root.crt");
    Files.writeString(
        multiCert,
        CertificateHelper.getAsPemString(ROOT_CA.getRight())
            + CertificateHelper.getAsPemString(next.getRight()));
    UUID temporary = UUID.randomUUID();
    CertificateInfo.create(
        temporary,
        customer.getUuid(),
        "tmp",
        new Date(),
        new Date(),
        "",
        multiCert.toString(),
        CertConfigType.K8SCertManager);
    universe = setDetails(d -> d.rootCA = temporary);
    Pair<String, String> old = newCertPem(Duration.ofDays(5));
    writeLocal(old);
    givenSecret(newCertPem(Instant.now().plus(Duration.ofDays(90)), next));

    assertEquals(rootCA, RpcClientCert.getOwningCA(temporary));
    assertEquals(certFile.toString(), rpcClientCert.getFiles(universe).get().getLeft());
    assertEquals(old.getLeft(), localCert());
  }

  @Test
  public void testSharedByUniversesOnSameCA() throws Exception {
    writeLocal(newCertPem(Duration.ofDays(60)));
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

    assertEquals(rpcClientCert.getFiles(universe), rpcClientCert.getFiles(other));
  }

  @Test
  public void testNotPresentedWhenNotRequired() throws Exception {
    writeLocal(newCertPem(Duration.ofDays(60)));
    dropFlag();

    assertTrue(rpcClientCert.getFiles(universe).isEmpty());
  }

  @Test
  public void testIssuedByFollowsIntermediate() throws Exception {
    Pair<KeyPair, X509Certificate> intermediate = newCA("intermediate-ca", ROOT_CA);
    Pair<String, String> leaf = newCertPem(Instant.now().plus(Duration.ofDays(90)), intermediate);
    String chain = leaf.getLeft() + CertificateHelper.getAsPemString(intermediate.getRight());

    assertTrue(RpcClientCert.isIssuedBy(chain, rootCA));
    assertFalse(RpcClientCert.isIssuedBy(leaf.getLeft(), rootCA));
  }

  private static Pair<String, String> newEcCertPem(Duration validFor) throws Exception {
    KeyPairGenerator generator = KeyPairGenerator.getInstance("EC");
    generator.initialize(256);
    KeyPair keyPair = generator.generateKeyPair();
    X509Certificate cert =
        sign(new X500Name("CN=yugabyte"), keyPair.getPublic(), Instant.now().plus(validFor), ROOT_CA);
    return Pair.of(
        CertificateHelper.getAsPemString(cert),
        CertificateHelper.getAsPemString(keyPair.getPrivate()));
  }

  @Test
  public void testAcceptsEcKey() throws Exception {
    Pair<String, String> pem = newEcCertPem(Duration.ofDays(90));
    givenSecret(pem);

    rpcClientCert.load(universe);
    rpcClientCert.load(universe);

    assertEquals(pem.getLeft(), localCert());
    verify(mockKubernetesManager, times(1)).getSecret(any(), anyString(), any());
  }

  @Test
  public void testKeepsEcCertWhenIssuerSignsWithAnotherCA() throws Exception {
    Pair<String, String> local = newEcCertPem(Duration.ofDays(5));
    writeLocal(local);
    givenSecret(fromOtherCA());

    assertTrue(rpcClientCert.getFiles(universe).isPresent());
    assertEquals(local.getLeft(), localCert());
  }

  @Test
  public void testReplacesMismatchedEcKey() throws Exception {
    Pair<String, String> pem = newEcCertPem(Duration.ofDays(60));
    writeLocal(Pair.of(pem.getLeft(), newEcCertPem(Duration.ofDays(60)).getRight()));
    givenSecret(pem);

    rpcClientCert.load(universe);

    assertEquals(pem.getRight(), Files.readString(keyFile));
  }

  @Test
  public void testClientCertRequiredFollowsIntent() {
    assertTrue(RpcClientCert.isClientCertRequired(universe));
    dropFlag();
    assertFalse(RpcClientCert.isClientCertRequired(universe));
  }

  @Test
  public void testValidateAcceptsSupportedSetup() {
    RpcClientCert.validate(universe.getUniverseDetails());
  }

  @Test
  public void testValidateRejectsUnsupportedSetups() throws Exception {
    universe = setDetails(d -> d.getPrimaryCluster().userIntent.enableNodeToNodeEncrypt = false);
    assertRejected(universe.getUniverseDetails(), "requires node-to-node encryption");

    universe = setDetails(d -> d.getPrimaryCluster().userIntent.enableNodeToNodeEncrypt = true);
    UUID selfSigned = createCA(CertConfigType.SelfSigned, ROOT_CA.getRight());
    universe = setDetails(d -> d.rootCA = selfSigned);
    assertRejected(universe.getUniverseDetails(), "requires a K8SCertManager root CA");
    universe = setDetails(d -> d.rootCA = null);
    assertRejected(universe.getUniverseDetails(), "requires a K8SCertManager root CA");

    universe =
        setDetails(
            d -> {
              d.rootCA = rootCA;
              d.getPrimaryCluster().userIntent.providerType = CloudType.aws;
            });
    assertRejected(universe.getUniverseDetails(), "only supported on Kubernetes universes");
  }

  private static void assertGFlagsRejected(
      UserIntent intent, boolean useClientCerts, String message) {
    PlatformServiceException e =
        assertThrows(
            PlatformServiceException.class,
            () -> GFlagsUtil.validateNodeToNodeClientCerts(intent, useClientCerts));
    assertTrue(e.getMessage(), e.getMessage().contains(message));
  }

  @Test
  public void testGFlagsRejectIdentityChecks() {
    UserIntent intent = new UserIntent();
    intent.tserverGFlags = Map.of(GFlagsUtil.NODE_TO_NODE_REQUIRED_UID, "yugabyte");
    assertGFlagsRejected(intent, true, GFlagsUtil.NODE_TO_NODE_REQUIRED_UID);
    // Allowed on a universe without client cert auth.
    GFlagsUtil.validateNodeToNodeClientCerts(intent, false);

    intent.tserverGFlags = Map.of();
    intent.masterGFlags = Map.of(GFlagsUtil.VERIFY_CLIENT_ENDPOINT, "true");
    assertGFlagsRejected(intent, true, GFlagsUtil.VERIFY_CLIENT_ENDPOINT);
  }

  @Test
  public void testGFlagsRejectTheFlagSetByHand() {
    // Even to turn client cert auth on: the intent field owns it.
    UserIntent intent = new UserIntent();
    intent.masterGFlags = Map.of(GFlagsUtil.USE_CLIENT_CERTIFICATES, "true");
    assertGFlagsRejected(intent, false, "Don't set");

    intent.masterGFlags = Map.of();
    intent.specificGFlags = SpecificGFlags.construct(Map.of(), Map.of());
    PerProcessFlags azFlags = new PerProcessFlags();
    azFlags.value = Map.of(ServerType.TSERVER, Map.of(GFlagsUtil.USE_CLIENT_CERTIFICATES, "false"));
    intent.specificGFlags.setPerAZ(new HashMap<>(Map.of(az.getUuid(), azFlags)));
    assertGFlagsRejected(intent, true, "Don't set");
    // Universes that set it to false before this check still pass.
    GFlagsUtil.validateNodeToNodeClientCerts(intent, false);
  }

  @Test
  public void testSecretName() {
    universe = setDetails(d -> d.useNewHelmNamingStyle = false);
    assertEquals(
        "yugabyte-tls-client-cert", CertManagerClientCert.getSecretName(universe, az.getUuid()));

    universe = setDetails(d -> d.useNewHelmNamingStyle = true);
    String secretName = CertManagerClientCert.getSecretName(universe, az.getUuid());
    assertTrue(secretName, secretName.matches("ybdemo-universe-[a-z0-9]+-client-tls"));
  }

  @Test
  public void testHelmOverrides() {
    // Any chart key algorithm is accepted.
    GFlagsUtil.validateNodeToNodeClientCertsOfHelmOverrides(
        "tls:\n  certManager:\n    certificates:\n      algorithm: ECDSA\n", null, true);
    String gflag = "gflags:\n  tserver:\n    " + GFlagsUtil.USE_CLIENT_CERTIFICATES + ": true\n";
    PlatformServiceException e =
        assertThrows(
            PlatformServiceException.class,
            () -> GFlagsUtil.validateNodeToNodeClientCertsOfHelmOverrides(gflag, null, false));
    assertTrue(e.getMessage(), e.getMessage().contains("Don't set"));

    String uid = "gflags:\n  master:\n    " + GFlagsUtil.NODE_TO_NODE_REQUIRED_UID + ": yugabyte\n";
    assertThrows(
        PlatformServiceException.class,
        () -> GFlagsUtil.validateNodeToNodeClientCertsOfHelmOverrides(uid, null, true));
    GFlagsUtil.validateNodeToNodeClientCertsOfHelmOverrides(uid, null, false);
  }
}
