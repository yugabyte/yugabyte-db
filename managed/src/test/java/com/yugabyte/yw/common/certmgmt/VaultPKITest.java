/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/
 *  POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.certmgmt;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.certmgmt.providers.CertificateProviderInterface;
import com.yugabyte.yw.common.certmgmt.providers.VaultPKI;
import com.yugabyte.yw.common.certmgmt.providers.VaultPKI.VaultOperationsForCert;
import com.yugabyte.yw.common.kms.util.hashicorpvault.HashicorpVaultConfigParams;
import com.yugabyte.yw.common.kms.util.hashicorpvault.VaultAccessor;
import com.yugabyte.yw.common.kms.util.hashicorpvault.VaultEARServiceUtilTest;
import java.security.PublicKey;
import java.security.cert.X509Certificate;
import java.util.Calendar;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import org.bouncycastle.asn1.x509.GeneralName;
import org.bouncycastle.asn1.x509.GeneralNames;
import org.junit.Before;
import org.junit.Test;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

// @RunWith(MockitoJUnitRunneoriginalObjr.class)
public class VaultPKITest extends FakeDBApplication {
  protected static final Logger LOG = LoggerFactory.getLogger(VaultPKITest.class);

  boolean MOCK_RUN;

  HashicorpVaultConfigParams params;

  UUID caUUID = UUID.randomUUID();
  UUID customerUUID = UUID.randomUUID();

  String username;
  String certPath;
  String certKeyPath;

  @Before
  public void setUp() {

    MOCK_RUN = VaultEARServiceUtilTest.MOCK_RUN;
    // MOCK_RUN = false;

    params = new HashicorpVaultConfigParams();
    params.vaultAddr = VaultEARServiceUtilTest.vaultAddr;
    params.vaultToken = VaultEARServiceUtilTest.vaultToken;
    params.engine = "pki";
    params.mountPath = "p3jan/";
    params.role = "example-dot-com";

    username = "127.0.0.1";
    certPath = username + ".pem";
    certKeyPath = username + ".key";
  }

  @Test
  public void TestPathCreation() {
    String oPath = "pki/cert/ca";
    String path =
        "pki/"
            + VaultOperationsForCert.CERT.toString()
            + "/"
            + VaultOperationsForCert.CA_CERT.toString();
    assertEquals(oPath, path);
  }

  @Test
  public void TestHashicorpParams() {
    HashicorpVaultConfigParams params2 = new HashicorpVaultConfigParams(params);
    Calendar tokenTtlExpiry = Calendar.getInstance();
    params2.ttl = 0L;
    params2.ttlExpiry = tokenTtlExpiry.getTimeInMillis();

    LOG.debug(" Json string:{}", params2.toString());
    JsonNode node = params2.toJsonNode();

    assertNotNull(node);
    assertEquals("0", node.get("HC_VAULT_TTL").asText());
    assertEquals(String.valueOf(params2.ttlExpiry), node.get("HC_VAULT_TTL_EXPIRY").asText());
  }

  @Test
  public void TestCertificateGenerationMock() throws Exception {
    if (MOCK_RUN == false) return;

    Map<String, String> result = new HashMap<String, String>();
    result.put(VaultPKI.ISSUE_FIELD_SERIAL, TestUtils.readResource("hashicorp/pki/ca.pem"));
    result.put(VaultPKI.ISSUE_FIELD_CERT, TestUtils.readResource("hashicorp/pki/client.pem"));
    result.put(VaultPKI.ISSUE_FIELD_PRV_KEY, TestUtils.readResource("hashicorp/pki/client.key"));
    result.put(VaultPKI.ISSUE_FIELD_CA, TestUtils.readResource("hashicorp/pki/issuing_ca.pem"));

    VaultAccessor vAccessor = mock(VaultAccessor.class);

    when(vAccessor.writeAt(any(), any())).thenReturn(result);

    VaultPKI pkiObj = new VaultPKI(caUUID, vAccessor, params);

    pkiObj.createCertificate(null, username, new Date(), new Date(), certPath, certKeyPath, null);

    X509Certificate cert = CertificateHelper.convertStringToX509Cert(pkiObj.getCertPEM());
    LOG.info(CertificateHelper.getCertificateProperties(cert));

    assertEquals("CN=" + username, cert.getSubjectDN().toString());
    assertTrue(cert.getSubjectAlternativeNames().toString().contains(username));
  }

  @Test
  public void TestCertificateGeneration() throws Exception {

    if (MOCK_RUN == true) return;

    // Simulates CertificateController.getClientCert
    CertificateProviderInterface certProvider = VaultPKI.getVaultPKIInstance(caUUID, params);
    CertificateDetails cDetails =
        certProvider.createCertificate(null, username, null, null, certPath, certKeyPath, null);

    // LOG.info("Client Cert is: {}", cDetails.getCrt());
    // LOG.info("Client Cert key is: {}", cDetails.getKey());

    X509Certificate cert = CertificateHelper.convertStringToX509Cert(cDetails.getCrt());

    LOG.info(CertificateHelper.getCertificateProperties(cert));

    assertEquals("CN=" + username, cert.getSubjectDN().toString());
    assertTrue(cert.getSubjectAlternativeNames().toString().contains(username));

    // verify using issue_ca
    X509Certificate caCert =
        CertificateHelper.convertStringToX509Cert(((VaultPKI) certProvider).getCACertificate());
    PublicKey k = caCert.getPublicKey();
    cert.verify(k, "BC");

    // verify by fetching ca from mountPath.
    caCert = ((VaultPKI) certProvider).getCACertificateFromVault();
    assertNotEquals("", caCert);
    k = caCert.getPublicKey();
    cert.verify(k, "BC");
  }

  @Test
  public void TestCertificateDates() throws Exception {

    if (MOCK_RUN == true) return;

    Date certStart = Calendar.getInstance().getTime();
    Calendar calEnd = Calendar.getInstance();
    calEnd.add(Calendar.DATE, 2);
    Date certExpiry = calEnd.getTime();

    CertificateProviderInterface certProvider = VaultPKI.getVaultPKIInstance(caUUID, params);
    CertificateDetails cDetails =
        certProvider.createCertificate(
            null, username, certStart, certExpiry, certPath, certKeyPath, null);
    X509Certificate cert = CertificateHelper.convertStringToX509Cert(cDetails.crt);
    LOG.info("Start Date: {} and {}", certStart.toString(), cert.getNotBefore());
    LOG.info("End Date: {} and {}", certExpiry.toString(), cert.getNotAfter());

    long diffInMillies1 = Math.abs(certExpiry.getTime() - certStart.getTime());
    long diffInMillies2 = Math.abs(cert.getNotAfter().getTime() - cert.getNotBefore().getTime());
    long ttlInHrs1 = TimeUnit.HOURS.convert(diffInMillies1, TimeUnit.MILLISECONDS);
    long ttlInHrs2 = TimeUnit.HOURS.convert(diffInMillies2, TimeUnit.MILLISECONDS);

    assertEquals(0L, Math.abs(ttlInHrs1 - ttlInHrs2));
  }

  @Test
  public void TestCertificateIPs() throws Exception {

    if (MOCK_RUN == true) return;

    String ip1 = "127.0.0.111";
    String ip2 = "127.0.0.112";
    Map<String, Integer> alternateNames = new HashMap<>();

    alternateNames.put(ip1, GeneralName.iPAddress);
    alternateNames.put(ip2, GeneralName.iPAddress);

    // alternateNames.put("otherName", GeneralName.otherName);
    alternateNames.put("rfc822Name", GeneralName.rfc822Name); // string
    alternateNames.put("dNSName", GeneralName.dNSName); // string
    // alternateNames.put("x400Address", GeneralName.x400Address);
    // alternateNames.put("directoryName", GeneralName.directoryName);
    // alternateNames.put("ediPartyName", GeneralName.ediPartyName);
    alternateNames.put("uniformResourceIdentifier", GeneralName.uniformResourceIdentifier); // str
    // alternateNames.put("registeredID", GeneralName.registeredID);

    GeneralNames generalNames = CertificateHelper.extractGeneralNames(alternateNames);
    LOG.debug("SAN LIST: " + generalNames.toString());

    CertificateProviderInterface certProvider = VaultPKI.getVaultPKIInstance(caUUID, params);
    CertificateDetails cDetails =
        certProvider.createCertificate(
            null, username, null, null, certPath, certKeyPath, alternateNames);

    X509Certificate cert = CertificateHelper.convertStringToX509Cert(cDetails.crt);
    // LOG.info(CertificateHelper.getCertificateProperties(cert));

    String ip_sans = cert.getSubjectAlternativeNames().toString();
    assertTrue(ip_sans.contains(ip1));
    assertTrue(ip_sans.contains(ip2));

    assertTrue(ip_sans.contains("dNSName"));
  }

  @Test
  public void TestCAChainMock() throws Exception {
    if (MOCK_RUN == false) return;
    Map<String, String> result = new HashMap<String, String>();
    result.put("issuing_ca", TestUtils.readResource("hashicorp/pki/issuing_ca.pem"));
    result.put("ca_chain", TestUtils.readResource("hashicorp/pki/ca_chain.pem"));

    VaultAccessor vAccessor = mock(VaultAccessor.class);
    when(vAccessor.readAt(any(), any()))
        .thenReturn(result.get("issuing_ca"))
        .thenReturn(result.get("ca_chain"));

    VaultPKI pkiObj = new VaultPKI(caUUID, vAccessor, params);

    X509Certificate caCert = pkiObj.getCACertificateFromVault();
    List<X509Certificate> caChain = pkiObj.getCAChainFromVault();

    assertNotNull(caCert);
    assertEquals(2, caChain.size());

    caChain = CertificateHelper.convertStringToX509CertList(result.get("ca_chain"));
    assertEquals(2, caChain.size());
    caChain.clear();
  }

  @Test
  public void TestCAChainNonMock() throws Exception {
    if (MOCK_RUN == true) return;

    {
      VaultPKI certProvider = VaultPKI.getVaultPKIInstance(caUUID, params);
      X509Certificate caCert = certProvider.getCACertificateFromVault();
      List<X509Certificate> caChain = certProvider.getCAChainFromVault();

      assertNotNull(caCert);
      assertEquals(0, caChain.size());
    }
    {
      // for this test you would need a pki at p01feb_i as intermidiate pki
      /*
      create intermidiate ca:
      export pki=p01feb
      export pki_i="p01feb_i"
      export role_i=r2
      export ip="s.test.com"
      vault secrets enable -path=$pki_i pki
      vault secrets tune -max-lease-ttl=43800h $pki_i
      vault write $pki_i/intermediate/generate/internal \
       common_name="test.com Intermediate Authority" ttl=43800h \
       -format=json | jq -r '.data.csr' > pki_i.csr
          # *** dump output of above command in pki_i.csr
      vault write $pki/root/sign-intermediate csr=@pki_i.csr format=pem_bundle ttl=43800h
       -format=json | jq -r .data.certificate > i_signed.pem
          # also append ca of root PKI.
      cat i_signed.pem
        -----BEGIN CERTIFICATE-----
        MIIDpjCCA...eIvcEWTgAt5QGAqJg=
        -----END CERTIFICATE-----
        -----BEGIN CERTIFICATE-----
        MIIDLDCCAhSgAwIBAgIU...8/pr5ahdKEu7n+ngYv8QY1rRv
        -----END CERTIFICATE-----
      vault write $pki_i/intermediate/set-signed certificate=@i_signed.pem

      vault write $pki_i/config/urls \
      issuing_certificates="http://127.0.0.1:8200/v1/p01feb_i/ca" \
      crl_distribution_points="http://127.0.0.1:8200/v1/p01feb_i/crl"
      vault write $pki_i/roles/$role_i  allowed_domains=test.com \
       allow_subdomains=true max_ttl=720h
      vault write $pki_i/issue/$role_i common_name="$ip" ttl="800h" \
      -format=json > $ip.txt
      cat $ip.txt | jq -r '.data.certificate' > "$ip"_cert.pem
      cat $ip.txt | jq -r '.data.private_key' > "$ip"_key.pem
      cat $ip.txt | jq -r '.data.issuing_ca' > "$ip"_ca.pem
      cat $ip.txt | jq -r '.data.ca_chain[]' > "$ip"_ca_chain.pem

      store "$ip"_ca_chain.pem in hashicorp/pki/ca_chain.pem
      */

      String fileData = TestUtils.readResource("hashicorp/pki/ca_chain.pem");
      fileData = fileData.replaceAll("\\n$", "");

      List<X509Certificate> caChain = CertificateHelper.convertStringToX509CertList(fileData);
      assertEquals(2, caChain.size());
      caChain.clear();

      HashicorpVaultConfigParams params2 = new HashicorpVaultConfigParams(params);
      params2.mountPath = "p01feb_i/";
      VaultPKI certProvider = VaultPKI.getVaultPKIInstance(caUUID, params2);
      X509Certificate caCert = certProvider.getCACertificateFromVault();

      String returnedData = certProvider.getCAChainFromVaultInString();
      assertEquals(fileData, returnedData);

      caChain = certProvider.getCAChainFromVault();

      assertNotNull(caCert);
      assertEquals(2, caChain.size());
    }
  }

  @Test
  public void testRoleFailure() {
    if (MOCK_RUN == true) return;

    HashicorpVaultConfigParams params2 = new HashicorpVaultConfigParams(params);
    params2.role = "invalid_role";
    try {
      VaultPKI.getVaultPKIInstance(caUUID, params2);
      assertTrue(false);
    } catch (Exception e) {
      assertTrue(true);
    }
  }
}
