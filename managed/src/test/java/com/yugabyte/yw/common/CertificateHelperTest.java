// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;

import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import play.libs.Json;

import org.mockito.runners.MockitoJUnitRunner;

import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.security.InvalidKeyException;
import java.security.NoSuchAlgorithmException;
import java.security.NoSuchProviderException;
import java.security.SignatureException;
import java.security.cert.CertificateException;
import java.security.cert.CertificateFactory;

import java.security.cert.X509Certificate;


import java.util.UUID;
import java.util.Calendar;
import java.util.Date;
import static org.junit.Assert.*;


@RunWith(MockitoJUnitRunner.class)
public class CertificateHelperTest extends FakeDBApplication{

  private Customer c;

  private String certPath;

  @Before
  public void setUp() {
    c = ModelFactory.testCustomer();
    certPath = String.format("/tmp/certs/%s/", c.uuid);
  }

  @After
  public void tearDown() throws IOException {
    FileUtils.deleteDirectory(new File("/tmp/certs"));
  }

  @Test
  public void testCreateRootCAWithoutClientCert() {
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.nodePrefix = "test-universe";
    UUID rootCA = CertificateHelper.createRootCA(taskParams.nodePrefix, c.uuid, "/tmp");
    assertNotNull(CertificateInfo.get(rootCA));
    try {
      InputStream in = new FileInputStream(certPath + String.format("/%s/ca.root.crt", rootCA));
      CertificateFactory factory = CertificateFactory.getInstance("X.509");
      X509Certificate cert = (X509Certificate) factory.generateCertificate(in);
      assertEquals(cert.getIssuerDN(), cert.getSubjectDN());
    } catch (Exception e){
      fail(e.getMessage());
    }
  }

  @Test
  public void testCreateRootCAWithClientCert() {
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.nodePrefix = "test-universe";
    UUID rootCA = CertificateHelper.createRootCA(taskParams.nodePrefix, c.uuid, "/tmp");
    CertificateHelper.createClientCertificate(rootCA, String.format(certPath + "/%s",
                                                                    rootCA),
                                              "yugabyte", null, null);
    assertNotNull(CertificateInfo.get(rootCA));
    try {
      InputStream in = new FileInputStream(certPath + String.format("/%s/ca.root.crt", rootCA));
      CertificateFactory factory = CertificateFactory.getInstance("X.509");
      X509Certificate cert = (X509Certificate) factory.generateCertificate(in);
      assertEquals(cert.getIssuerDN(), cert.getSubjectDN());
      FileInputStream is = new FileInputStream(new File(certPath +
          String.format("/%s/yugabytedb.crt", rootCA)));
      X509Certificate clientCer = (X509Certificate) factory.generateCertificate(is);
      clientCer.verify(cert.getPublicKey(), "BC");
    } catch (Exception e){
      fail(e.getMessage());
    }
  }

  @Test
  public void testCreateCustomerCertToString() throws CertificateException,
    NoSuchAlgorithmException,InvalidKeyException, NoSuchProviderException,
    SignatureException, IOException {
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.nodePrefix = "test-universe";
    UUID rootCA = CertificateHelper.createRootCA(taskParams.nodePrefix, c.uuid, "/tmp");
    assertNotNull(CertificateInfo.get(rootCA));

    CertificateInfo cert = CertificateInfo.get(rootCA);
    FileInputStream is = new FileInputStream(new File(cert.certificate));
    CertificateFactory fact = CertificateFactory.getInstance("X.509");
    X509Certificate cer = (X509Certificate) fact.generateCertificate(is);

    Calendar cal = Calendar.getInstance();
    Date certStart = cal.getTime();
    cal.add(Calendar.YEAR, 1);
    Date certExpiry = cal.getTime();
    JsonNode result = CertificateHelper.createClientCertificate(rootCA, null, "postgres",
                                                                certStart, certExpiry);
    String clientCert = result.get("yugabytedb.crt").asText();
    assertNotNull(clientCert);
    ByteArrayInputStream bytes = new ByteArrayInputStream(clientCert.getBytes());
    X509Certificate clientCer = (X509Certificate) fact.generateCertificate(bytes);

    clientCer.verify(cer.getPublicKey(), "BC");
  }

  @Test
  public void testCreateCustomerCertToFile() throws CertificateException, NoSuchAlgorithmException,
    InvalidKeyException, NoSuchProviderException, SignatureException, IOException {
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.nodePrefix = "test-universe";
    UUID rootCA = CertificateHelper.createRootCA(taskParams.nodePrefix, c.uuid, "/tmp");
    assertNotNull(CertificateInfo.get(rootCA));

    CertificateInfo cert = CertificateInfo.get(rootCA);
    FileInputStream is = new FileInputStream(new File(cert.certificate));
    CertificateFactory fact = CertificateFactory.getInstance("X.509");
    X509Certificate cer = (X509Certificate) fact.generateCertificate(is);

    Calendar cal = Calendar.getInstance();
    Date certStart = cal.getTime();
    cal.add(Calendar.YEAR, 1);
    Date certExpiry = cal.getTime();
    JsonNode result = CertificateHelper.createClientCertificate(rootCA,
        String.format("/tmp", rootCA), "postgres", certStart, certExpiry);

    is = new FileInputStream(new File(String.format("/tmp/yugabytedb.crt", rootCA)));
    X509Certificate clientCer = (X509Certificate) fact.generateCertificate(is);

    clientCer.verify(cer.getPublicKey(), "BC");
  }

  private String getCertContent() {
    return "-----BEGIN CERTIFICATE-----\n" +
      "MIIDDjCCAfagAwIBAgIGAXVXb5y/MA0GCSqGSIb3DQEBCwUAMDQxHDAaBgNVBAMM\n" +
      "E3liLWFkbWluLXRlc3QtYXJuYXYxFDASBgNVBAoMC2V4YW1wbGUuY29tMB4XDTIw\n" +
      "MTAyMzIxNDg1M1oXDTIxMTAyMzIxNDg1M1owNDEcMBoGA1UEAwwTeWItYWRtaW4t\n" +
      "dGVzdC1hcm5hdjEUMBIGA1UECgwLZXhhbXBsZS5jb20wggEiMA0GCSqGSIb3DQEB\n" +
      "AQUAA4IBDwAwggEKAoIBAQCVWSZiQhr9e+L2MkSXP38dwXwF7RlZGhrYKrL7pp6l\n" +
      "aHkLZ0lsFgxI6h0Yyn5S+Hhi/jGWbBso6kXw7frUwVY5kX2Q6iv+E+rKqbYQgNV3\n" +
      "0vpCtOmNNolhNN3x4SKAIXyKOB5dXMGesjvba/qD6AstKS8bvRCUZcYDPjIUQGPu\n" +
      "cYLmywV/EdXgB+7WLhUOOY2eBRWBrnGxk60pcHJZeW44g1vas9cfiw81OWVp5/V5\n" +
      "apA631bE0MTgg283OCyYz+CV/YtnytUTg/+PUEqzM2cWsWdvpEz7TkKYXinRdN4d\n" +
      "SwgOQEIvb7A/GYYmVf3yk4moUxEh4NLoV9CBDljEBUjZAgMBAAGjJjAkMBIGA1Ud\n" +
      "EwEB/wQIMAYBAf8CAQEwDgYDVR0PAQH/BAQDAgLkMA0GCSqGSIb3DQEBCwUAA4IB\n" +
      "AQAFR0m7r1I3FyoatuLBIG+alaeGHqsgNqseAJTDGlEyajGDz4MT0c76ZIQkTSGM\n" +
      "vsM49Ad2D04sJR44/WlI2AVijubzHBr6Sj8ZdB909nPvGtB+Z8OnvKxJ0LUKyG1K\n" +
      "VUbcCnN3qSoVeY5PaPeFMmWF0Qv4S8lRTZqAvCnk34bckwkWoHkuuNGO49CsNb40\n" +
      "Z2NBy9Ckp0KkfeDpGcv9lHuUrl13iakCY09irvYRbfi0lVGF3+wXZtefV8ZAxfnN\n" +
      "Vt4faawkJ79oahlXDYs6WCKEd3zVM3mR3STnzwxvtB6WacjhqgP4ozXdt6PUbTfZ\n" +
      "jZPSP3OuL0IXk96wFHScay8S\n" +
      "-----END CERTIFICATE-----\n";
  }

  @Test
  public void testUploadRootCA() {
    Calendar cal = Calendar.getInstance();
    Date certStart = cal.getTime();
    cal.add(Calendar.YEAR, 1);
    Date certExpiry = cal.getTime();
    UUID rootCA = null;
    CertificateInfo.Type type = CertificateInfo.Type.CustomCertHostPath;
    String cert_content = getCertContent();

    try {
      rootCA = CertificateHelper.uploadRootCA("test", c.uuid, "/tmp", cert_content, null,
          certStart, certExpiry, type, null);
    } catch (Exception e) {
      fail(e.getMessage());
    }
    assertNotNull(CertificateInfo.get(rootCA));
  }

  @Test
  public void testUploadRootCAWithInvalidCertContent() {
    Calendar cal = Calendar.getInstance();
    Date certStart = cal.getTime();
    cal.add(Calendar.YEAR, 1);
    Date certExpiry = cal.getTime();
    UUID rootCA = null;
    CertificateInfo.Type type = CertificateInfo.Type.CustomCertHostPath;

    try {
      rootCA = CertificateHelper.uploadRootCA("test", c.uuid, "/tmp", "invalid_cert", null,
        certStart, certExpiry, type, null);
    } catch (Exception e) {
      assertEquals("Unable to get cert Object", e.getMessage());
    }
  }

  @Test
  public void testSelfSignedCertUploadRootCA() {
    Calendar cal = Calendar.getInstance();
    Date certStart = cal.getTime();
    cal.add(Calendar.YEAR, 1);
    Date certExpiry = cal.getTime();
    UUID rootCA = null;
    CertificateInfo.Type type = CertificateInfo.Type.SelfSigned;
    String cert_content = getCertContent();
    try {
      rootCA = CertificateHelper.uploadRootCA("test", c.uuid, "/tmp", cert_content, "test_key",
        certStart, certExpiry, type, null);
    } catch (Exception e) {
      assertEquals("Invalid certificate.", e.getMessage());
    }
  }
}
