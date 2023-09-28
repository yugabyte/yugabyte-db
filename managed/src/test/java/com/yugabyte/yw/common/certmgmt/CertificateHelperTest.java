// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.certmgmt;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.fail;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.spy;

import com.typesafe.config.Config;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
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
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class CertificateHelperTest extends FakeDBApplication {

  private Customer c;

  private String certPath;
  private Config spyConf;
  private CertificateHelper certificateHelper;

  @Mock private RuntimeConfGetter mockConfGetter;

  @Before
  public void setUp() {
    c = ModelFactory.testCustomer();
    certPath =
        String.format(
            "/tmp" + File.separator + getClass().getSimpleName() + File.separator + "certs/%s",
            c.getUuid());
    spyConf = spy(app.config());
    doReturn("/tmp/" + getClass().getSimpleName()).when(spyConf).getString("yb.storage.path");
    new File(certPath).mkdirs();
    certificateHelper = new CertificateHelper(mockConfGetter);
  }

  @After
  public void tearDown() throws IOException {
    FileUtils.deleteDirectory(new File("/tmp/" + getClass().getSimpleName() + "/certs"));
  }

  @Test
  public void testCreateRootCAWithoutClientCert() {
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.nodePrefix = "test-universe";
    UUID rootCA = certificateHelper.createRootCA(spyConf, taskParams.nodePrefix, c.getUuid());
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
    UUID rootCA = certificateHelper.createRootCA(spyConf, taskParams.nodePrefix, c.getUuid());
    CertificateHelper.createClientCertificate(
        spyConf, rootCA, String.format(certPath + "/%s", rootCA), "yugabyte", null, null);
    assertNotNull(CertificateInfo.get(rootCA));
    try {
      InputStream in = new FileInputStream(certPath + String.format("/%s/ca.root.crt", rootCA));
      CertificateFactory factory = CertificateFactory.getInstance("X.509");
      X509Certificate cert = (X509Certificate) factory.generateCertificate(in);
      assertEquals(cert.getIssuerDN(), cert.getSubjectDN());
      FileInputStream is =
          new FileInputStream(certPath + String.format("/%s/yugabytedb.crt", rootCA));
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
    UUID clientRootCA =
        certificateHelper.createClientRootCA(spyConf, taskParams.nodePrefix, c.getUuid());
    CertificateHelper.createClientCertificate(
        spyConf,
        clientRootCA,
        String.format(certPath + "/%s", clientRootCA),
        "yugabyte",
        null,
        null);
    assertNotNull(CertificateInfo.get(clientRootCA));
    try {
      InputStream in =
          new FileInputStream(certPath + String.format("/%s/ca.root.crt", clientRootCA));
      CertificateFactory factory = CertificateFactory.getInstance("X.509");
      X509Certificate cert = (X509Certificate) factory.generateCertificate(in);
      assertEquals(cert.getIssuerDN(), cert.getSubjectDN());
      FileInputStream is =
          new FileInputStream(certPath + String.format("/%s/yugabytedb.crt", clientRootCA));
      X509Certificate clientCer = (X509Certificate) factory.generateCertificate(is);
      clientCer.verify(cert.getPublicKey(), "BC");
    } catch (Exception e) {
      fail(e.getMessage());
    }
  }

  @Test
  public void testCreateCustomerCertToString()
      throws CertificateException,
          NoSuchAlgorithmException,
          InvalidKeyException,
          NoSuchProviderException,
          SignatureException,
          IOException {
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.nodePrefix = "test-universe";
    UUID rootCA = certificateHelper.createRootCA(spyConf, taskParams.nodePrefix, c.getUuid());
    assertNotNull(CertificateInfo.get(rootCA));

    CertificateInfo cert = CertificateInfo.get(rootCA);
    FileInputStream is = new FileInputStream(cert.getCertificate());
    CertificateFactory fact = CertificateFactory.getInstance("X.509");
    X509Certificate cer = (X509Certificate) fact.generateCertificate(is);

    Calendar cal = Calendar.getInstance();
    Date certStart = cal.getTime();
    cal.add(Calendar.YEAR, 1);
    Date certExpiry = cal.getTime();
    CertificateDetails result =
        CertificateHelper.createClientCertificate(
            spyConf, rootCA, null, "postgres", certStart, certExpiry);
    String clientCert = result.crt;
    assertNotNull(clientCert);
    ByteArrayInputStream bytes = new ByteArrayInputStream(clientCert.getBytes());
    X509Certificate clientCer = (X509Certificate) fact.generateCertificate(bytes);

    clientCer.verify(cer.getPublicKey(), "BC");
  }

  @Test
  public void testCreateCustomerCertToFile()
      throws CertificateException,
          NoSuchAlgorithmException,
          InvalidKeyException,
          NoSuchProviderException,
          SignatureException,
          IOException {
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.nodePrefix = "test-universe";
    UUID rootCA = certificateHelper.createRootCA(spyConf, taskParams.nodePrefix, c.getUuid());
    assertNotNull(CertificateInfo.get(rootCA));

    CertificateInfo cert = CertificateInfo.get(rootCA);
    FileInputStream is = new FileInputStream(cert.getCertificate());
    CertificateFactory fact = CertificateFactory.getInstance("X.509");
    X509Certificate cer = (X509Certificate) fact.generateCertificate(is);

    Calendar cal = Calendar.getInstance();
    Date certStart = cal.getTime();
    cal.add(Calendar.YEAR, 1);
    Date certExpiry = cal.getTime();
    String certPath = String.format("/tmp/%s", rootCA);

    if (new File(certPath).mkdirs()) {
      CertificateHelper.createClientCertificate(
          spyConf, rootCA, String.format("/tmp/%s", rootCA), "postgres", certStart, certExpiry);

      is = new FileInputStream(String.format("/tmp/%s/yugabytedb.crt", rootCA));
      X509Certificate clientCer = (X509Certificate) fact.generateCertificate(is);

      clientCer.verify(cer.getPublicKey(), "BC");
    } else {
      fail();
    }
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
        + "MIIEnjCCAoYCCQCaL+r4/wbT2TANBgkqhkiG9w0BAQUFADARMQ8wDQYDVQQDDAZU\n"
        + "ZXN0Q0EwHhcNMjEwNzA2MDM1MjI3WhcNNDgxMTIxMDM1MjI3WjARMQ8wDQYDVQQD\n"
        + "DAZUZXN0Q0EwggIiMA0GCSqGSIb3DQEBAQUAA4ICDwAwggIKAoICAQDT+tYHdM00\n"
        + "08vLjDXcji6bq85T1PnvddLDM97Yy48XFEkzqI+kTK7JsXHF6hsZv7xpsri6E1oU\n"
        + "hPNqtZb5Q/8vYGbQwG3eeZF/iRgdQyVVRZ3vUUhBcxLRfIz2541kMtnFI5a9pXu4\n"
        + "MIertzCA1ZQcmrWUkG+jer3KoagNY+d2N4GQ/KZEyQ6vLfVRSBJliqzlc8NIAZy8\n"
        + "iA7hO1Ilz9+o++8k/+AweOYxJRJNNvgiWdJTCxImHtANyw/VLd4Nc+lgcrxI7Vqp\n"
        + "g63nX1JsCR4QS/PmWkcQ3gn303rYhkUha9KpMkvd5MBdS8zMA2gQZIrCAiqGO+Tz\n"
        + "yXMP5SK5Cok5DTFJ4hFyVabX38T4G/mF8NrpWLuQ1iodjJXdp4gKHj1MwEH4MlnM\n"
        + "Dt5sJgqYY2O+A/SkttUqfsvp+sZBEN23WUKThJZ+DAJip/XJbdEEOW5IqMLvt/gy\n"
        + "dd3YF6oIuOdGg+u+lDv5HPL0QQW+BtQs36Er4LRj3kuUcd9Iq0Ado9QTbHArmZu/\n"
        + "4Pjiox1Vj9vpfS1+5VTyUWRW/rJp6iKLDUHIvydHMYourhmK2QiCV0yc4a1WU+zL\n"
        + "eKwCC3RP1R/xw6qh5HeHyer34q7oD0yg4/FV0HrdIgZMNKIru3PX+wk6kraCXxFV\n"
        + "KeLL+a66wkeKwrLSqp1b+XE8YAzdxK8iHwIDAQABMA0GCSqGSIb3DQEBBQUAA4IC\n"
        + "AQAtmutvNAVfB2pse7dqyfVubAaQQGwfAfwvhopE4ucrjoqakHNJA+cCi0Dg0sCE\n"
        + "YqM0J1sV106wPJFKZEqoBx96ppmC4fato+ZrjHlEEw82OZoaf2IA4exu/nPavTL+\n"
        + "OgUYWL0e0RNeb4qzvdxsic8uyfm7UIaSU32jBQNqdtrHD7EFKqSJgR705AvEwDXv\n"
        + "Q+8UUaq1ZXmn7ZPJN52YNEkzmRivhBvEsarsuQylmWFBJPZpcfjxDkVuZv5smt/3\n"
        + "sdTWs2TBk/Md/daebfvSJhu2UZPyrIJPV2dZeVtyRInTZIbQzyevWXbczKxjJok6\n"
        + "xeZj5YdNBS+BMx55+BbUFtiHjGyUvBxmUu+6SAh5Isu1bfZDYvZLDQGjrESHBkPA\n"
        + "yye8MXuSE3iRIL0DG/Zc7vvT8Uvhe9ikPQvsOBLeH5KMD+Dt6K/pR+/OzW90HFg5\n"
        + "GNAhYKeVlsDopFiu4/gNcJ5W0R/ZSrnqe7SW6hPj0ih5iic61dtkRU4f78RinBz/\n"
        + "dHwQpoI6YkN7qgVjUViJVeqK9HdK/z8Os7tBLPnO6O3GOesK/uEnEbj1rKyqaIHv\n"
        + "oH6qmXrFiEKTx9PLnTXOB+eDefRlHOfqOcvbRMuxlJZhOye/X3VGF4CmL9SHyphb\n"
        + "s/DlIEVrpImIWPyEcZufODb9Tf8rSNeh1ld162peRP14NA==\n"
        + "-----END CERTIFICATE-----\n"
        + "-----BEGIN CERTIFICATE-----\n"
        + "MIIEnjCCAoYCCQDaHoaiKPWfSTANBgkqhkiG9w0BAQsFADARMQ8wDQYDVQQDDAZU\n"
        + "ZXN0Q0EwHhcNMjEwNzA2MDM1MTM4WhcNNDgxMTIxMDM1MTM4WjARMQ8wDQYDVQQD\n"
        + "DAZUZXN0Q0EwggIiMA0GCSqGSIb3DQEBAQUAA4ICDwAwggIKAoICAQD2ajXoTu/2\n"
        + "Y5DUIw5k8kcTnqlQ75VL6GtzaE5twsw/PGiF2Zg0Fi/5ucNZ1zKW7LbTxjIKOn9Q\n"
        + "vPV1TN+4NC5qcJ4QzYDQ6hSGeb3KShuLrz+rMapZXf29eqsa3+lDSVlZs3j73iw9\n"
        + "hK+zDadIM7l2dA4tuVUmpHrWdKwpkzZ8gPlgjyIiLQHT2PylPkb59o97WAgbBOza\n"
        + "1rTWfibf1sn3NWyE4stk7bkqbFUva/RSWbS6F00NgMAoxS9a5VvUOeIXP+C+Qlxv\n"
        + "pVetfBp5t9XBWBcvYnqE872AKUgZLhwWnouX+ggWp1xPFrvdaMsbzJ6dv0EICimb\n"
        + "b2GB8FWrepTWDgRnj/EbfN8+lZsZo0HC5Fimu/jSdhr5xyeWqeDA18p2eNBMkJpa\n"
        + "A+zAeZJel+5h1kgikPan6fLylvn8BYS1q30tW+H2gKbDeN8L67ebKT+dmVInxQTH\n"
        + "s/I3Ykh0a2YQN6oaZCvuRcJz38FF440kbVLXylPBsBlrRO7q7PAjzFcHuGhVOHXg\n"
        + "4R2I3AAvR5crhc6SAEnSQLYVP0Z1AGA5GtUU4bfNOcHoTwgbY1ZQ9goGkdKUEl8L\n"
        + "OKn8uNUHQitoeg3BJJvUKAxeiOD8UwfdB96ZSpVNG79zZ14A9Jx2FAfRdZpaQwhb\n"
        + "BVv+zTGHBevkr7lzb3sNJc5edkiRzbCMaQIDAQABMA0GCSqGSIb3DQEBCwUAA4IC\n"
        + "AQBJNochpd6JrTDKduvm10iiIi86BORLjp7wrCLJ6QRFhL0BrYdhSlwZKnPG4vaH\n"
        + "f5TfxgPDXOqIkAWVyA5ZrDNxI7M/aFH2szOih3e9qQxiF5ba2xhCiRX4krCnAeJi\n"
        + "JPdUEX4hXszD2iUjcB8Q5ps8Zrz//OFZHVzIXZ1QUrvo4ssE2oFkh1BrrIsR7MDb\n"
        + "34Hyc1idt3Thgpi3xSX0NvueSLQGKCQdjKoc+oYJbDJicNz+LtcIj3+NXnB4i190\n"
        + "7oRi1VyHtCwF0LnHF1oAPYoEdJs/TmKeYm+C1CzeP/pDGirprYkcj4uGPho/oYq2\n"
        + "jlvHF+ciUcYiWkZRtdDpV30aAafQublmT3aBq+FeJYIJh+z92Ro04kESd8yLNAzq\n"
        + "N0ux9+qJmbGMLpoL1WEG6fDhtKprQ47gyZ+jLh+HnSvXuVZlqa1pCaf5umQrHOyC\n"
        + "Sl4aRGut8rQJrTFlQUfUgm9qIxkbT0A29U+KpwILPbhnQEpBvVyytv9EjjPm1noG\n"
        + "ySYGXUFPhPCjt5m9vgAGvW/9FsZw6Z6ElVYNABVnB5WWF7I3gPH0pdDonEAxBQ8T\n"
        + "/ruF+o5xK790JT8SamOqUTVULHYPJTujE27ntxHavxTDEhZKRyt/OyzSYW2ebrNj\n"
        + "FLo1iihMDPrSQFkXPUJeafVKeQzWRzsPRMyLDwwWa202RQ==\n"
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

  public static String getServerKeyContent() {
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
    UUID rootCA = null;
    CertConfigType type = CertConfigType.CustomCertHostPath;
    String certContent = getCertContent();

    try {
      rootCA =
          CertificateHelper.uploadRootCA(
              "test", c.getUuid(), "/tmp", certContent, null, type, null, null);
    } catch (Exception e) {
      fail(e.getMessage());
    }
    assertNotNull(CertificateInfo.get(rootCA));
    try {
      assertEquals(
          certContent,
          FileUtils.readFileToString(
              new File(CertificateInfo.get(rootCA).getCertificate()), "UTF-8"));
    } catch (IOException e) {
      fail(e.getMessage());
    }
  }

  @Test
  public void testUploadRootCAWithExpiredCertValidity() {
    CertConfigType type = CertConfigType.CustomServerCert;
    String certContent = getRootCertContent();
    CertificateParams.CustomServerCertData customServerCertData =
        new CertificateParams.CustomServerCertData();
    customServerCertData.serverCertContent = getServerCertContent();
    customServerCertData.serverKeyContent = getServerKeyContent();

    try {
      CertificateHelper.uploadRootCA(
          "test", c.getUuid(), "/tmp", certContent, null, type, null, customServerCertData);
    } catch (Exception e) {

      assertEquals(
          "Certificate with CN = CA for YugabyteDB" + " has invalid start/end dates.",
          e.getMessage());
    }
  }

  @Test
  public void testUploadRootCAWithInvalidCertContent() {
    try {
      CertificateHelper.uploadRootCA(
          "test",
          c.getUuid(),
          "/tmp",
          "invalid_cert",
          null,
          CertConfigType.CustomCertHostPath,
          null,
          null);
    } catch (Exception e) {
      assertEquals("Unable to get cert Objects", e.getMessage());
    }
  }

  @Test
  public void testSelfSignedCertUploadRootCA() {
    CertConfigType type = CertConfigType.SelfSigned;
    String cert_content = getCertContent();
    try {
      CertificateHelper.uploadRootCA(
          "test", c.getUuid(), "/tmp", cert_content, "test_key", type, null, null);
    } catch (Exception e) {
      assertEquals("Certificate and key don't match.", e.getMessage());
    }
  }

  @Test
  public void testSelfSignedIncorrectCertUploadRootCA() {
    CertConfigType type = CertConfigType.SelfSigned;
    String cert_content = getIncorrectCertContent();
    try {
      CertificateHelper.uploadRootCA(
          "test", c.getUuid(), "/tmp", cert_content, "test_key", type, null, null);
    } catch (Exception e) {
      assertEquals(
          "Certificate with CN = Cloud Intermediate has no associated root", e.getMessage());
    }
  }
}
