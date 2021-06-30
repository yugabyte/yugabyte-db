// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.forms.CertificateParams;
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
import static org.junit.Assert.assertNull;
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
  public void testCreateClientRootCAWithClientCert() {
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.nodePrefix = "test-universe";
    UUID clientRootCA = CertificateHelper.createClientRootCA(taskParams.nodePrefix, c.uuid, "/tmp");
    CertificateHelper.createClientCertificate(
        clientRootCA, String.format(certPath + "/%s", clientRootCA), "yugabyte", null, null);
    assertNotNull(CertificateInfo.get(clientRootCA));
    try {
      InputStream in =
          new FileInputStream(certPath + String.format("/%s/ca.root.crt", clientRootCA));
      CertificateFactory factory = CertificateFactory.getInstance("X.509");
      X509Certificate cert = (X509Certificate) factory.generateCertificate(in);
      assertEquals(cert.getIssuerDN(), cert.getSubjectDN());
      FileInputStream is =
          new FileInputStream(
              new File(certPath + String.format("/%s/yugabytedb.crt", clientRootCA)));
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

  public String getRootCertContent() {
    return "-----BEGIN CERTIFICATE-----\n"
        + "MIIDBzCCAe+gAwIBAgIJAN1cUUTzWvAeMA0GCSqGSIb3DQEBCwUAMC8xETAPBgNV\n"
        + "BAoMCFl1Z2FieXRlMRowGAYDVQQDDBFDQSBmb3IgWXVnYWJ5dGVEQjAeFw0yMTA2\n"
        + "MjUxMTM3MDRaFw0yMTA3MjUxMTM3MDRaMC8xETAPBgNVBAoMCFl1Z2FieXRlMRow\n"
        + "GAYDVQQDDBFDQSBmb3IgWXVnYWJ5dGVEQjCCASIwDQYJKoZIhvcNAQEBBQADggEP\n"
        + "ADCCAQoCggEBAMnexKC2A6Gjh3q9IW1q7xOpV9WNIIPCVYw8NcVWDCRaL/F2ZcdU\n"
        + "xNnmNV8zDMhzEatKrvW+UmTnt0gtSrFuaTcNPg7ZY/+9fe20M2J4iIs1yCPNkp8E\n"
        + "uBDDn50hxYqIIWbteGqZnhA33JnG47kaAexFw1wzTgjv8MVPf7r0BOqSTYvQKQzf\n"
        + "zfpdRxRs0ZaCRLKr7N1502j3v+wcAkVHqfMgHUjl/plbIgefBmWrg9TtXxTAEFQY\n"
        + "n19fnKIxb1g7Jm3C5tpafxG05cqLquxNOTN+t9j7IIeeg+didkDDFoU/ZVKKxDwl\n"
        + "7Ue/v9Y5dkmVumU+175rUkMqjRdKXrrXzZMCAwEAAaMmMCQwDgYDVR0PAQH/BAQD\n"
        + "AgLkMBIGA1UdEwEB/wQIMAYBAf8CAQEwDQYJKoZIhvcNAQELBQADggEBADhtIwYv\n"
        + "OZuxlcPb6nFMIpLunKKtVeRLU8+pKs7QNvjRJHniyyifcaEA+Lt1oMu+VsEIvD5c\n"
        + "eL5RY5eQE0DKW0N/xroaqnlOTsh6hBFzJa1qb232H3IMf8zkPuYsmvFHhGhqlCrl\n"
        + "1XD5jIsfe7EKV23h/yhIhRA8upXr3fWO5rxXEIM0GlKi9duaP4UzFimrju3y20ya\n"
        + "8EzMhYye0tgMAzzXMz6Mh4KwsJ0TiQk2tq5n4zFFZYUDSxsc0zCSitXA0wWY1J1c\n"
        + "ETnX/l8825yH3cdMX9kdNtBjiQiVkfMG6tzrb1zMoAeDASYXzoWXCXXQnoDBi62C\n"
        + "BwpTUb3lHhf1PwA=\n"
        + "-----END CERTIFICATE-----\n";
  }

  public String getServerCertContent() {
    return "-----BEGIN CERTIFICATE-----\n"
        + "MIICyjCCAbICAQEwDQYJKoZIhvcNAQELBQAwLzERMA8GA1UECgwIWXVnYWJ5dGUx\n"
        + "GjAYBgNVBAMMEUNBIGZvciBZdWdhYnl0ZURCMB4XDTIxMDEwMTAxMDEwMVoXDTIx\n"
        + "MDEwMTExMTExMVowJzERMA8GA1UECgwIWXVnYWJ5dGUxEjAQBgNVBAMMCTEyNy4w\n"
        + "LjAuMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAMiHOV8Zux6OL/8B\n"
        + "EFvOjJZzEs3XyhVLFqezUuxM3wrdHIGcjR9ATBhZT7F4dTSexueHuxUjLD4Pq62h\n"
        + "Cma2TXggpJpHw1/zVg5xVuxfbhfZqp+n7HTeqKygRH5KboL9NHjC0eSekmt+uyrT\n"
        + "ofWWTGHvJdaRv3QJXiwKCQSlg8obOX3qufriIqhpUAJDkNIWZPtlJ670/O0jsMnV\n"
        + "3nM4WfD534uHy6QJegChX/0+4Dg5SLDPXV4SHmbvKmtdE3VbKb+/zFTc9h+kickd\n"
        + "Qh217BsolkJJVdGA8Ycf1gTJ20goCrgwNaiqKLLqIqDDLtSn5fjxmZrQuKUJa8GP\n"
        + "p+TTsXcCAwEAATANBgkqhkiG9w0BAQsFAAOCAQEAthmM6+3Ubrqw7N4Jh7wA0m0I\n"
        + "OXjb0uVKzPfjZnmCqYGUixyBMeBPUbjwAMfV/dYnMmf/JB1tWaK/1XaaxY6itUi+\n"
        + "9xO1McGvftJnKFm6rMIYzaQ0YaqL3JS210z9ejVir6UeEXU0jqN0W2NKIqQynz1e\n"
        + "0sRoLpMVlyddvRc+lndlL9KhWpmmEgon9csrwwRQnOjgD03kwnruEA3qn9F4qoiU\n"
        + "19dTZemP1FV3FmChyrgLmhcuFlT3+yvq6b3J6bFOjnB1MNtqvHa8G0YDcnqPFHdw\n"
        + "neMHsNU4eLYRtUZfGJBS5ndvfke3RdDAyLicOdnveKlkqAFRBH6JhFs0yR/yiw==\n"
        + "-----END CERTIFICATE-----";
  }

  public String getServerKeyContent() {
    return "-----BEGIN RSA PRIVATE KEY-----\n"
        + "MIIEpQIBAAKCAQEAyIc5Xxm7Ho4v/wEQW86MlnMSzdfKFUsWp7NS7EzfCt0cgZyN\n"
        + "H0BMGFlPsXh1NJ7G54e7FSMsPg+rraEKZrZNeCCkmkfDX/NWDnFW7F9uF9mqn6fs\n"
        + "dN6orKBEfkpugv00eMLR5J6Sa367KtOh9ZZMYe8l1pG/dAleLAoJBKWDyhs5feq5\n"
        + "+uIiqGlQAkOQ0hZk+2UnrvT87SOwydXeczhZ8Pnfi4fLpAl6AKFf/T7gODlIsM9d\n"
        + "XhIeZu8qa10TdVspv7/MVNz2H6SJyR1CHbXsGyiWQklV0YDxhx/WBMnbSCgKuDA1\n"
        + "qKoosuoioMMu1Kfl+PGZmtC4pQlrwY+n5NOxdwIDAQABAoIBAQCgdeRovxRGnQy3\n"
        + "B0jpzdwdv7M6WARzCYT1aL9gKxsHfGuFI5qheTfq+/yTTIqtMqiDOM9xWJXci3mb\n"
        + "FJRYIGTZTXWppucl7nfcUsF1n99e4mRwIVVLJ6jdidmFHVZVJvxH7c07HdCYh+6J\n"
        + "lJOzCzPP4ifrDPGgjqZ2owkYWMGehMPbU1Sqa5MuUHZ4lIViJ96ysCcGZw483bC1\n"
        + "FWqL/kJOG361oLxFUkiexVhtxDDunWOx/F32UQdWmQ0ji/HQGygrTCyciwmO9Fri\n"
        + "e91qd1Xtnpbt1HY3vXG5E4sgXU2zblBVBbOEIxSjzxsiAhCG3JPA0vXLe+KzSB9m\n"
        + "AAfhpabBAoGBAOSV/Rzg8LL1F+sn33mUOw+EqzHRiR6L20+1/gBlnV+w+Z8rfu9V\n"
        + "L34scaqQbqMiRC2gyZasXsf6d1vtGTgyg8qGeVJ2pJKtr+wUI8HRlNTIsd85bJCw\n"
        + "caLbz5EybudObmnxOzT2T9hlzrHljfnUE4xpP/P92E5e5ixP1J+Rro7RAoGBAOCT\n"
        + "0A0EioPVKKh/Ojn8RX6N4WeuZEyRR2As7xiEk01fL33PK+QtRfRxWB9SGS+C0y/c\n"
        + "doV+DG2c8LIH2iJgMcSNrRtd6hjdRc6sxRfM81lHBM4w2HRZA5+X8m8dvo55WWJG\n"
        + "0Lhct5ZRe3ndOgSGC94W5opuKtTLUCKzbgLv5R3HAoGBAOIWrBxFLC2FF9xCOtow\n"
        + "z93Adec3fa0V3ZxQwGM6HlcIX02cotcr5WahpOd0Jcn62X5b2yfJY5HeXPIyZ9Ba\n"
        + "vlmxegwjRxHA8xDItrk8hz2TJ8NHM+dEbZEMYpgVTvY/cBwfOlVfDohV4gO4rh2d\n"
        + "MpydeWDmAW06leTcLMyLNiERAoGAcwyGWwTmiR8cUyXKiIYqe+jnfpwime8bJ7Qd\n"
        + "UOwqIksPI16M17oxykZ+pJ0EdgiJdE13EnA+obDyxeGr6Exrcow6EHkOQmqpJnXw\n"
        + "Cn9ec3AkkBUJ7qDjcFEBS9drFcYo+mXY63nkO0fG/+lLnaGmVsJYnLZPjvARP5W4\n"
        + "WBzZvWUCgYEA0KIJHBT4CYClNCmW6ke1q3LJ1SX1qDEm60+uSyvFOb8sH88FvXL3\n"
        + "3PUAhU4tqp3vRRgmFViqcfucIBnWlwWNwD4nErOa/NgiDBeFLH7wa5f1mtG9H3bs\n"
        + "iisE7+k1/Oi0oQKdmuOpyUjx/L58uYhoCy4NXaXzo0k+sz7MqS7jlq0=\n"
        + "-----END RSA PRIVATE KEY-----\n";
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
              "test", c.uuid, "/tmp", certContent, null, certStart, certExpiry, type, null, null);
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
  public void testUploadRootCAWithExpiredCertValidity() {
    Calendar cal = Calendar.getInstance();
    Date certStart = cal.getTime();
    cal.add(Calendar.DATE, 1);
    Date certExpiry = cal.getTime();
    CertificateInfo.Type type = CertificateInfo.Type.CustomServerCert;
    String certContent = getRootCertContent();
    CertificateParams.CustomServerCertData customServerCertData =
        new CertificateParams.CustomServerCertData();
    customServerCertData.serverCertContent = getServerCertContent();
    customServerCertData.serverKeyContent = getServerKeyContent();

    try {
      CertificateHelper.uploadRootCA(
          "test",
          c.uuid,
          "/tmp",
          certContent,
          null,
          certStart,
          certExpiry,
          type,
          null,
          customServerCertData);
    } catch (Exception e) {
      assertEquals("Certificate with CN = 127.0.0.1 has invalid start/end dates.", e.getMessage());
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
          "test", c.uuid, "/tmp", "invalid_cert", null, certStart, certExpiry, type, null, null);
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
          "test",
          c.uuid,
          "/tmp",
          cert_content,
          "test_key",
          certStart,
          certExpiry,
          type,
          null,
          null);
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
          "test",
          c.uuid,
          "/tmp",
          cert_content,
          "test_key",
          certStart,
          certExpiry,
          type,
          null,
          null);
    } catch (Exception e) {
      assertEquals(
          "Certificate with CN = Cloud Intermediate has no associated root", e.getMessage());
    }
  }
}
