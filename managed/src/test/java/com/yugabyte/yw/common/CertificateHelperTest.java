// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
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
import java.util.Calendar;
import java.util.Date;
import java.util.UUID;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.fail;

@RunWith(MockitoJUnitRunner.class)
public class CertificateHelperTest extends FakeDBApplication {

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
    } catch (Exception e) {
      fail(e.getMessage());
    }
  }

  @Test
  public void testCreateRootCAWithClientCert() {
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.nodePrefix = "test-universe";
    UUID rootCA = CertificateHelper.createRootCA(taskParams.nodePrefix, c.uuid, "/tmp");
    CertificateHelper.createClientCertificate(
        rootCA, String.format(certPath + "/%s", rootCA), "yugabyte", null, null);
    assertNotNull(CertificateInfo.get(rootCA));
    try {
      InputStream in = new FileInputStream(certPath + String.format("/%s/ca.root.crt", rootCA));
      CertificateFactory factory = CertificateFactory.getInstance("X.509");
      X509Certificate cert = (X509Certificate) factory.generateCertificate(in);
      assertEquals(cert.getIssuerDN(), cert.getSubjectDN());
      FileInputStream is =
          new FileInputStream(new File(certPath + String.format("/%s/yugabytedb.crt", rootCA)));
      X509Certificate clientCer = (X509Certificate) factory.generateCertificate(is);
      clientCer.verify(cert.getPublicKey(), "BC");
    } catch (Exception e) {
      fail(e.getMessage());
    }
  }

  @Test
  public void testCreateCustomerCertToString()
      throws CertificateException, NoSuchAlgorithmException, InvalidKeyException,
          NoSuchProviderException, SignatureException, IOException {
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
    JsonNode result =
        CertificateHelper.createClientCertificate(rootCA, null, "postgres", certStart, certExpiry);
    String clientCert = result.get("yugabytedb.crt").asText();
    assertNotNull(clientCert);
    ByteArrayInputStream bytes = new ByteArrayInputStream(clientCert.getBytes());
    X509Certificate clientCer = (X509Certificate) fact.generateCertificate(bytes);

    clientCer.verify(cer.getPublicKey(), "BC");
  }

  @Test
  public void testCreateCustomerCertToFile()
      throws CertificateException, NoSuchAlgorithmException, InvalidKeyException,
          NoSuchProviderException, SignatureException, IOException {
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
    CertificateHelper.createClientCertificate(
        rootCA, String.format("/tmp", rootCA), "postgres", certStart, certExpiry);

    is = new FileInputStream(new File(String.format("/tmp/yugabytedb.crt", rootCA)));
    X509Certificate clientCer = (X509Certificate) fact.generateCertificate(is);

    clientCer.verify(cer.getPublicKey(), "BC");
  }

  private String getIncorrectCertContent() {
    return "-----BEGIN CERTIFICATE-----\n"
        + "MIIC9zCCAd+gAwIBAgIGAXndeeP3MA0GCSqGSIb3DQEBCwUAMDcxETAPBgNVBAoM\n"
        + "CFl1Z2FieXRlMSIwIAYDVQQDDBlDQSBmb3IgWXVnYWJ5dGUgQ2xvdWQgRGV2MB4X\n"
        + "DTIxMDYwNTE4NDAyM1oXDTMxMDYwNTE4NDAyM1owHTEbMBkGA1UEAwwSQ2xvdWQg\n"
        + "SW50ZXJtZWRpYXRlMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAkdxV\n"
        + "98Lp2Fd2CZ33I7dv+tsK0YtzhquD1LSate5YW6KptfrWkOeTNkLImH9Og9jYqLnJ\n"
        + "8egzCOOpiA9bRKLCdE6XFLGSaAoEhiujIZZwB3KyuUlym7pdAj+3HCqJia9mlGt+\n"
        + "0FO3FtWMEPZ0kcLGdH2+9bZI+pTyPF8PAIlDjdpruLwKoFviwfPAui3+0lj0fP2Z\n"
        + "lKdUkrCOEtqeNrWL5oar9H6beTAeI2oLSHodfqS9e3+xuNTXE3lL9f2nbUcTaMHV\n"
        + "mOB7ar/gc+unmyEiewDMSlOWlcv9JJVZGkhvcNPMakiKo/xLnWSQ7nQK2euABIgr\n"
        + "MBUYUpu3QjqvdtHb8wIDAQABoyMwITAPBgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB\n"
        + "/wQEAwIC5DANBgkqhkiG9w0BAQsFAAOCAQEAaqa8LPtWgsxz8pAZ1DAiUV/VqyAN\n"
        + "QJgJPZAaz2/L/SdYDtuCtNSbhmlACr7NhXRpbJG7pBRf5VZ7nDnqGx51TnU7SWmI\n"
        + "IYKrCduAVQzhhH7J0FIhVL45totqCdHhs9kyTkcc3bdXHxh6OHbp0RtrcJFJmAos\n"
        + "keJjXfFcQYI/sKFfJi9EzdHazUfTeZ2Ctw0wz0kT1soCX1Dh8R8okciyc9KuUX3h\n"
        + "usvfCiGdPGbKIiTo1usmUqdIpy6KR+U9AKYeHsVLJ95Vt7uB88Dk0yv/rNyYYX2M\n"
        + "eQvfgu8K31lOTWfiBA9N6cbvwoZmmmeRf/v74K280MLIwtOMI1WZsw3pQg==\n"
        + "-----END CERTIFICATE-----\n"
        + "-----BEGIN CERTIFICATE-----\n"
        + "MIIC9zCCAd+gAwIBAgIGAXnsjOOIMA0GCSqGSIb3DQEBCwUAMDcxETAPBgNVBAoM\n"
        + "CFl1Z2FieXRlMSIwIAYDVQQDDBlDQSBmb3IgWXVnYWJ5dGUgQ2xvdWQgRGV2MB4X\n"
        + "DTIxMDYwODE2NTUyNloXDTMxMDYwODE2NTUyNlowHTEbMBkGA1UEAwwSQ2xvdWQg\n"
        + "SW50ZXJtZWRpYXRlMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAgM4e\n"
        + "IBvTiKlFS/G1Ov7c+Cr72YkNs26AMx+b/5Q/DMR7rg8Gwd3LiDkXBUTk7meFt2Fd\n"
        + "kLzkx2uwTG+AaSYMMB2LKZvP1ZbwZbU7RbHW6jtWF8AGoR2EQJv9ix6arJEmQOQ+\n"
        + "6V0PTFcprcERpXOj975VEZZ6IhqFveDOI0OlkGsOSXjseXeSUqRyXlqKbTEGZ1+u\n"
        + "SE1D+AKZQs5qpLBqjo989rC08QIjaocQC65F5RNgqdJFOgR9JxUPTtK0CS6Ft7gH\n"
        + "e0E1R1oc/3Y87acdRiBCbP9mhamE66gEh1kduihWZ4+ap+NPBIv66/QG5HM/3Kj5\n"
        + "tOz74MSSeIZwhyOfhQIDAQABoyMwITAPBgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB\n"
        + "/wQEAwIC5DANBgkqhkiG9w0BAQsFAAOCAQEAKAyYqvXbiiPMnb12Mry1TJ96gjZK\n"
        + "gpHZZEzFTvCaEOfshTozCrdwch6bp4KvAxTeDlceQdMnhAAulvlcsuebL0kTrvfD\n"
        + "vuqRLm+Md5AGgCp1xWzAaYza8iiWDccnB42aJjnACCxAeId/B23P8FpASVpws/6m\n"
        + "yu2sEZ+Oa+xPOzuPn8hYlDRnKQtWgqsDHfnpVqf4LGpJs70/7+au3MJ+vJ8N2Xtp\n"
        + "MB/EMrDPcQwMqA/Ga24+YLtT4ij0iSVFYA9xOYlSOjDzmdzYuU9w/ec8PaPosIVu\n"
        + "KV/PRFQdim4TzRx41Q2PQ30YdMd8RCE7umM8PB/tVHHkMBVCh3ZcMSo2Lg==\n"
        + "-----END CERTIFICATE-----\n";
  }

  private String getCertContent() {
    return "-----BEGIN CERTIFICATE-----\n"
        + "MIIC9zCCAd+gAwIBAgIGAXndeeP3MA0GCSqGSIb3DQEBCwUAMDcxETAPBgNVBAoM\n"
        + "CFl1Z2FieXRlMSIwIAYDVQQDDBlDQSBmb3IgWXVnYWJ5dGUgQ2xvdWQgRGV2MB4X\n"
        + "DTIxMDYwNTE4NDAyM1oXDTMxMDYwNTE4NDAyM1owHTEbMBkGA1UEAwwSQ2xvdWQg\n"
        + "SW50ZXJtZWRpYXRlMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAkdxV\n"
        + "98Lp2Fd2CZ33I7dv+tsK0YtzhquD1LSate5YW6KptfrWkOeTNkLImH9Og9jYqLnJ\n"
        + "8egzCOOpiA9bRKLCdE6XFLGSaAoEhiujIZZwB3KyuUlym7pdAj+3HCqJia9mlGt+\n"
        + "0FO3FtWMEPZ0kcLGdH2+9bZI+pTyPF8PAIlDjdpruLwKoFviwfPAui3+0lj0fP2Z\n"
        + "lKdUkrCOEtqeNrWL5oar9H6beTAeI2oLSHodfqS9e3+xuNTXE3lL9f2nbUcTaMHV\n"
        + "mOB7ar/gc+unmyEiewDMSlOWlcv9JJVZGkhvcNPMakiKo/xLnWSQ7nQK2euABIgr\n"
        + "MBUYUpu3QjqvdtHb8wIDAQABoyMwITAPBgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB\n"
        + "/wQEAwIC5DANBgkqhkiG9w0BAQsFAAOCAQEAaqa8LPtWgsxz8pAZ1DAiUV/VqyAN\n"
        + "QJgJPZAaz2/L/SdYDtuCtNSbhmlACr7NhXRpbJG7pBRf5VZ7nDnqGx51TnU7SWmI\n"
        + "IYKrCduAVQzhhH7J0FIhVL45totqCdHhs9kyTkcc3bdXHxh6OHbp0RtrcJFJmAos\n"
        + "keJjXfFcQYI/sKFfJi9EzdHazUfTeZ2Ctw0wz0kT1soCX1Dh8R8okciyc9KuUX3h\n"
        + "usvfCiGdPGbKIiTo1usmUqdIpy6KR+U9AKYeHsVLJ95Vt7uB88Dk0yv/rNyYYX2M\n"
        + "eQvfgu8K31lOTWfiBA9N6cbvwoZmmmeRf/v74K280MLIwtOMI1WZsw3pQg==\n"
        + "-----END CERTIFICATE-----\n"
        + "-----BEGIN CERTIFICATE-----\n"
        + "MIIDIjCCAgqgAwIBAgIUfBXB1GzE+2caVXj2AZZnaEA3puYwDQYJKoZIhvcNAQEL\n"
        + "BQAwNzERMA8GA1UECgwIWXVnYWJ5dGUxIjAgBgNVBAMMGUNBIGZvciBZdWdhYnl0\n"
        + "ZSBDbG91ZCBEZXYwHhcNMjEwNjA0MTUwNTQzWhcNMjEwNzA0MTUwNTQzWjA3MREw\n"
        + "DwYDVQQKDAhZdWdhYnl0ZTEiMCAGA1UEAwwZQ0EgZm9yIFl1Z2FieXRlIENsb3Vk\n"
        + "IERldjCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAN7LLnQxIFnKcNU6\n"
        + "rrQ8eFYgMlFgxt9YIaUnSVeHQEyFgszfPbH9RZOhBEc58uSNkzLL/gC6XRomVzVG\n"
        + "btqhQ3UAAtngD14HKgkW+S+R2zMWpUuaYRathRy7xueq1qTU3qUyy/c75qmPtUdM\n"
        + "mN5cFNecGweTu0pqr6fakcASNtVl5SzxM5ZbRuVIopoEUDPnPozGw189GELZT0GX\n"
        + "otmfs0G6d/0Lws7MKclDVnIxQp/U9s8IrIuiucfGnTwKoPOjS5CAm7QgPQF7PGTb\n"
        + "972k5zEWQCvoG8HV5XAiu5/+9TXbXayvByfXbMP6B8e+wOxdwH3w11bwWvIcTBmb\n"
        + "7qrZlTsCAwEAAaMmMCQwDgYDVR0PAQH/BAQDAgLkMBIGA1UdEwEB/wQIMAYBAf8C\n"
        + "AQEwDQYJKoZIhvcNAQELBQADggEBAN0/zepCmi3O6YkCF1yIpAomOq2yaroNV10q\n"
        + "snECUPzmGzwwoUAks5zTmNd+6mSBNJjfq4cIo90S78fgPRoT6S0GPQf1K4Frxz2K\n"
        + "LIYajaZVO+ur3sSTxDBufXpKhciFfFOzGownmSJMfkJZIateK/IeT2I3+QMO6gRN\n"
        + "I87lMscEA1JbibRShIfw5wZ45B/HHmqOVo/h/eSCd8HxMUh/P5+AqsE+wxByER01\n"
        + "amumEQpXFOznGjROr7uYxI9k6TFgK7AXe9NHnP/nutAtKA6VZgwrvNiLH7bytY2Y\n"
        + "40y2FKV6VY59i9CeWnZpUJzNomGnLPm9a955r3523ar99HzWBtc=\n"
        + "-----END CERTIFICATE-----\n";
  }

  @Test
  public void testUploadRootCA() {
    Calendar cal = Calendar.getInstance();
    Date certStart = cal.getTime();
    cal.add(Calendar.YEAR, 1);
    Date certExpiry = cal.getTime();
    UUID rootCA = null;
    CertificateInfo.Type type = CertificateInfo.Type.CustomCertHostPath;
    String certContent = getCertContent();

    try {
      rootCA =
          CertificateHelper.uploadRootCA(
              "test", c.uuid, "/tmp", certContent, null, certStart, certExpiry, type, null);
    } catch (Exception e) {
      fail(e.getMessage());
    }
    assertNotNull(CertificateInfo.get(rootCA));
    try {
      assertEquals(
          certContent,
          FileUtils.readFileToString(new File(CertificateInfo.get(rootCA).certificate), "UTF-8"));
    } catch (IOException e) {
      fail(e.getMessage());
    }
  }

  @Test
  public void testUploadRootCAWithInvalidCertContent() {
    Calendar cal = Calendar.getInstance();
    Date certStart = cal.getTime();
    cal.add(Calendar.YEAR, 1);
    Date certExpiry = cal.getTime();
    CertificateInfo.Type type = CertificateInfo.Type.CustomCertHostPath;

    try {
      CertificateHelper.uploadRootCA(
          "test", c.uuid, "/tmp", "invalid_cert", null, certStart, certExpiry, type, null);
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
    CertificateInfo.Type type = CertificateInfo.Type.SelfSigned;
    String cert_content = getCertContent();
    try {
      CertificateHelper.uploadRootCA(
          "test", c.uuid, "/tmp", cert_content, "test_key", certStart, certExpiry, type, null);
    } catch (Exception e) {
      assertEquals("Certificate and key don't match.", e.getMessage());
    }
  }

  @Test
  public void testSelfSignedIncorrectCertUploadRootCA() {
    Calendar cal = Calendar.getInstance();
    Date certStart = cal.getTime();
    cal.add(Calendar.YEAR, 1);
    Date certExpiry = cal.getTime();
    CertificateInfo.Type type = CertificateInfo.Type.SelfSigned;
    String cert_content = getIncorrectCertContent();
    try {
      CertificateHelper.uploadRootCA(
          "test", c.uuid, "/tmp", cert_content, "test_key", certStart, certExpiry, type, null);
    } catch (Exception e) {
      assertEquals(
          "Certificate with CN = Cloud Intermediate has no associated root", e.getMessage());
    }
  }
}
