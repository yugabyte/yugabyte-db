// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;

import com.yugabyte.yw.forms.CertificateParams;
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
  public void testCreateRootCAWithClientAndPlatformCert() {
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.nodePrefix = "test-universe";
    UUID rootCA = CertificateHelper.createRootCA(taskParams.nodePrefix, c.uuid, "/tmp");
    assertNotNull(CertificateInfo.get(rootCA));
    assertNotNull(CertificateInfo.get(rootCA).platformCert);
    assertNotNull(CertificateInfo.get(rootCA).platformKey);
    try {
      InputStream in = new FileInputStream(certPath + String.format("/%s/ca.root.crt", rootCA));
      CertificateFactory factory = CertificateFactory.getInstance("X.509");
      X509Certificate cert = (X509Certificate) factory.generateCertificate(in);
      assertEquals(cert.getIssuerDN(), cert.getSubjectDN());
      FileInputStream is = new FileInputStream(new File(certPath +
          String.format("/%s/yugabytedb.crt", rootCA)));
      X509Certificate clientCer = (X509Certificate) factory.generateCertificate(is);
      clientCer.verify(cert.getPublicKey(), "BC");
      is = new FileInputStream(new File(certPath +
          String.format("/%s/platform.crt", rootCA)));
      clientCer = (X509Certificate) factory.generateCertificate(is);
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
    JsonNode result = CertificateHelper.createClientCertificate(rootCA, "postgres",
                                                                certStart, certExpiry, false);
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
        "postgres", certStart, certExpiry, true);

    is = new FileInputStream(new File(CertificateHelper.getClientCertFile(rootCA)));
    X509Certificate clientCer = (X509Certificate) fact.generateCertificate(is);

    clientCer.verify(cer.getPublicKey(), "BC");
  }

  private String ROOT_CERT1 = "-----BEGIN CERTIFICATE-----\n" +
      "MIIDEjCCAfqgAwIBAgIGAXj06P5KMA0GCSqGSIb3DQEBCwUAMDYxHjAcBgNVBAMM\n" +
      "FXliLWFkbWluLXRlc3QtYXJuYXYtMjEUMBIGA1UECgwLZXhhbXBsZS5jb20wHhcN\n" +
      "MjEwNDIxMTQ1MDEzWhcNMjIwNDIxMTQ1MDEzWjA2MR4wHAYDVQQDDBV5Yi1hZG1p\n" +
      "bi10ZXN0LWFybmF2LTIxFDASBgNVBAoMC2V4YW1wbGUuY29tMIIBIjANBgkqhkiG\n" +
      "9w0BAQEFAAOCAQ8AMIIBCgKCAQEAlIzYE0MPlOqlxId6c7/RfGibP0dStM7xy6gp\n" +
      "texcbpDz+xdtez8IvZbvI6u831qElgUh9s851kIr/9hE4v/pqPbqMqv25fyRns1u\n" +
      "6Qd+hH7jSkhYbeBGnIKlkB1dDAS3DFR3140p0ocGtZr5bfdp9bq2SHCZMpP13ihw\n" +
      "ZtpMErKczDVN0lXzJiPkTbXCu08HLibgB3naf53M1qi9OcnxLBybB8m8cCfTLhiI\n" +
      "CSeASleACDdyW01vCpzwPtvSH93HVXM9j2rDSsyI8WLvKFr0egwVFPiObuzcW6mG\n" +
      "/QwUyAnbE0XIW4Ws9iDTjeIUSDExaYwJBBTgh4zmXLXJnvnbvwIDAQABoyYwJDAS\n" +
      "BgNVHRMBAf8ECDAGAQH/AgEBMA4GA1UdDwEB/wQEAwIC5DANBgkqhkiG9w0BAQsF\n" +
      "AAOCAQEAbqZfTnVJ+HkuWt7jpcOuFBz5OIQlSalHnXmgqjW3u9SZXfWRmnhveKlq\n" +
      "Ual+WApGrcNz0GuRR5ObaORiNbpwWLO4cywygYP6zr/eMkoam2jyCY9IHu24osRJ\n" +
      "iPItFtJjakZ7AnpWZClgu3SL9WIlvk2gmFlad5HDlgM5UYW6V2CUATOpnQY0MUiK\n" +
      "LtXuDtSjRpPODhJks+bqFdy/RH0vUWMDEwEDXFWysodKFHPiidYFae7fxol+D/Oa\n" +
      "Am1yZxqMLIKhPER+Ww2UfHeXUzeAbAAljQ3BgmcVJMuzm9OuUQijJE05DDRNneWK\n" +
      "BvjpBP+N0rcGVrv5KfA5uexKbxK3pQ==\n" +
      "-----END CERTIFICATE-----\n";

  private String PLATFORM_CERT1 = "-----BEGIN CERTIFICATE-----\n" +
      "MIIDbDCCAlSgAwIBAgIGAXj06P6iMA0GCSqGSIb3DQEBCwUAMDYxHjAcBgNVBAMM\n" +
      "FXliLWFkbWluLXRlc3QtYXJuYXYtMjEUMBIGA1UECgwLZXhhbXBsZS5jb20wHhcN\n" +
      "MjEwNDIxMTQ1MDEzWhcNMjIwNDIxMTQ1MDEzWjATMREwDwYDVQQDDAh5dWdhYnl0\n" +
      "ZTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAJh7OtGYNOHTRfk9Q+kK\n" +
      "9Ona+mt1tIhdFAgAQiGS7KtGEP27sq3mT3IzESbm/pXDe8/qQaU+EppyNJLWb33/\n" +
      "8yrAZMbML3ExWItN9p1Vif/9WsLOm5+FJw64zMo1bHdi9hpk1eqvYnT+SWw6EmP+\n" +
      "d/794qV5shiKHqDGDHGGYaQK5mP7pw2j2AW60ffyHj/lA8/+DfqyJvqlZAUXd22j\n" +
      "MXj4gXSW4SEshZi57FIWeT5pwZXIpFuT9Kkg8I1kjs9ARtCuju7UIkoUxy35mgP1\n" +
      "wG9heLxzdOTC12zTTWN50WSogWsXMzlUn51paUwvTNPunYXjyNw7dBacycoz7xMK\n" +
      "7/UCAwEAAaOBojCBnzAMBgNVHRMBAf8EAjAAMGMGA1UdIwRcMFqAFAVu4DRhgUc8\n" +
      "eA1ZI5rJxBOc1ImcoTqkODA2MR4wHAYDVQQDDBV5Yi1hZG1pbi10ZXN0LWFybmF2\n" +
      "LTIxFDASBgNVBAoMC2V4YW1wbGUuY29tggYBePTo/kowHQYDVR0OBBYEFC5UMV8G\n" +
      "FRM0V5k4SCY7qdr1k1OvMAsGA1UdDwQEAwIC5DANBgkqhkiG9w0BAQsFAAOCAQEA\n" +
      "kETX4GIsGu2yUkfD3QJ0VUHV7ICu/kZLwVq5Q7tIgMxXiDn25RTdYQNlSvXq7B3Z\n" +
      "4zTBY2Jkw4s1+qs672IzFFlat4ViRdHoXoVTYIr5AhKD+b4S7+W/VUpcNu2Zw9pA\n" +
      "OVVfGKn5W9OYLQBB1+RRO7TeW9PMukuW5jz16ffDkdysG218fTcD33/uYh8D+1TO\n" +
      "SDWfh/qEwnMX5IHR1xpot7IIfRkKDwy7AOiTcfrmSS17kWDuylV57b47l5MfK6/1\n" +
      "uCeVSn8crDjHTN0SaoJplZ/f7epvvDTYu/pEDJjBH5VjnnQ3MIJLOBB4N/gQfygc\n" +
      "+r8aubazF6Cay9+Fp0W1xA==\n" +
      "-----END CERTIFICATE-----\n";

 private String PLATFORM_KEY1 = "-----BEGIN RSA PRIVATE KEY-----\n" +
    "MIIEogIBAAKCAQEAmHs60Zg04dNF+T1D6Qr06dr6a3W0iF0UCABCIZLsq0YQ/buy\n" +
    "reZPcjMRJub+lcN7z+pBpT4SmnI0ktZvff/zKsBkxswvcTFYi032nVWJ//1aws6b\n" +
    "n4UnDrjMyjVsd2L2GmTV6q9idP5JbDoSY/53/v3ipXmyGIoeoMYMcYZhpArmY/un\n" +
    "DaPYBbrR9/IeP+UDz/4N+rIm+qVkBRd3baMxePiBdJbhISyFmLnsUhZ5PmnBlcik\n" +
    "W5P0qSDwjWSOz0BG0K6O7tQiShTHLfmaA/XAb2F4vHN05MLXbNNNY3nRZKiBaxcz\n" +
    "OVSfnWlpTC9M0+6dhePI3Dt0FpzJyjPvEwrv9QIDAQABAoIBADqv80uIUZI5Rs1P\n" +
    "Dzw6w1jet1N00i9J49PQhaN2cTDant+JxpO3+QvzK77VWVc7DgRQHUQESBS5sBJ0\n" +
    "BiVwxZ7GvgLlw7zFSVcUgr67lYm5cZ1Y9/zFuuqnpeqN9Vld9WrjNJJHPpXY6VmG\n" +
    "YF2sK3MxNHKMDEf1oSQwFcn79siz7vcyyd2ic83qhgPFLi0QbEEP4j98tIqDARcr\n" +
    "MhdF4w6YsaeDokonLURxApH9L+Y1DC0UMVRcRsNVFxjnZcNrCS4zENReqUwUyA/C\n" +
    "UNFX6lMEjJHfyRcGZo4aLROFMYt8pg7ocrubkYVpMKog7fpD/R9jvwxJG47e0SD1\n" +
    "iYVah2ECgYEA6nC87ngWQmSIU6ftVXCDTXeEk5bnpkXxGeFNxQHcZTFziXwueZQE\n" +
    "khDRy5sTNzqvgzilRQBh/vcfwvD7Zkx4/xfWC2AJNcRNEXdlwT22PE+vxg+6uN52\n" +
    "I/LqIUTFjnGGxXP0xFyk9k1SYc8wv2O25xGmCUkRptrDJwIFkpehO/sCgYEApoD9\n" +
    "ROFsmxDammwo+f1um8fUIvOsvtN7F/DWMBT7Yxbtn4yxYNW6L1VClha9vOfUCeRR\n" +
    "6XWGnKGQihwPiQUqXpsWEVNdazx7lyODhW8KN3LD+G9gh4FPutGsKh0OqJiKDQpk\n" +
    "bQjfRPLvQ7MNaN6JzfcyJ1asXGBc182Np/Y3UM8CgYA46swDvWXyEoRCgyeMsbc/\n" +
    "DIBEcMOmy55AYUHHTa9bZgkd3OdPnw8JA0pb+zdMFlRcMFl3iiNAinDMnEL80B4k\n" +
    "GH5f3p39zr3DOtCafMgMlnAfTtxPW7sk+Sm8j/zCm29T6tYHAlMOdTFGC85S+PuD\n" +
    "1/YOlQ3TC5OLmeMOdv+vFwKBgDRh3EwxR0O5l6yBXprXWlb0FQ8x7iTSz6UGhbv2\n" +
    "vee8rOFHF/7I/pwpjJs/aE3n+VNrW0HBUaoxQhHRJioLT/sXe2fT/E3iZ1dzOstd\n" +
    "1a8AEhs0nv/CdNznXeyZ26S66KOVo2aSJfvBGfkIea2GZYBAxqyNnggp4MubxXcU\n" +
    "BO8NAoGAE6X9GEblaou8hiYCnsKZ/G8z2UaZ6KcKQ5gpgOI1riiyU23MBNzv2ILc\n" +
    "YNBBd/B6sV35fHxXTjxdKevRnrWoiiCrQQdHFTCeqGFElSI7kte1cyb5wUVTsHUj\n" +
    "i5EyNkznMwZdNNRftwTtzljSoHrMBnfOB3hGoJxCAjt7EQ5G1Ew=\n" +
    "-----END RSA PRIVATE KEY-----\n";

 private String ROOT_CERT2 = "-----BEGIN CERTIFICATE-----\n" +
    "MIIDDjCCAfagAwIBAgIGAXj052ygMA0GCSqGSIb3DQEBCwUAMDQxHDAaBgNVBAMM\n" +
    "E3liLWFkbWluLXRlc3QtYXJuYXYxFDASBgNVBAoMC2V4YW1wbGUuY29tMB4XDTIx\n" +
    "MDQyMTE0NDgzMFoXDTIyMDQyMTE0NDgzMFowNDEcMBoGA1UEAwwTeWItYWRtaW4t\n" +
    "dGVzdC1hcm5hdjEUMBIGA1UECgwLZXhhbXBsZS5jb20wggEiMA0GCSqGSIb3DQEB\n" +
    "AQUAA4IBDwAwggEKAoIBAQCTivhOhvh1VdAjccOq6XG2jkYDULRYr0qqNPTCfkQR\n" +
    "7T9EiRPaHjrxMyVIYwJ08qaq+khAEuc5cNP7F4XIO9U3SLLVK+6Vb3+pj2jn26rJ\n" +
    "f8FBjG5usMRm7RluRrwTvggJ2ft58+zIXe0R4TBN29PY/OWcjEBsLBVZziVzoEyq\n" +
    "nmcITiQHRaLHa3PzS8caOf38sYqlRyeSaAclqcuQIfyY1ARXtn++EbYkr2XsTVUt\n" +
    "DjgwJVT9yxxZRLiZvkdABSjPkktKRqPOTrf5HNY+kiUlgzX9M75BEGfZPX2K+eh/\n" +
    "f8VKEwN+6/c10QOlCP/Fw1gCKfpGT9iQ2/C4Xji6rjpNAgMBAAGjJjAkMBIGA1Ud\n" +
    "EwEB/wQIMAYBAf8CAQEwDgYDVR0PAQH/BAQDAgLkMA0GCSqGSIb3DQEBCwUAA4IB\n" +
    "AQBVzO23aIfGmoZkavOtiZdf55wg4rao9iYD0tbybPuTMKwe7tx3SNfYyK1Svsld\n" +
    "aIbmmfq0u4ucLnnwuUme57VSX01oqJXpqQ+26HqjmlqOCx6rByfO9dzsDAmsaBrz\n" +
    "zUzcEQWF6oq65+EGH2kSx/AIq99aT7wEnQSfNX4ndtBWi6D4jOeqP3rxDKhMoBrM\n" +
    "5Rt4p/DYG4TwviwbodLFQ91nzucxqpaoXZ1MeGVLbu3OfdX6M7/fcIsDJypovUdu\n" +
    "1sDKkbvlvRsM84NYlfNl6GMtP3ngdwYgVt7SKW9tkSTRCVTiRMEjJtO/tM0KTQLl\n" +
    "fC+1Thm+xd+8aTz8/r58MRb4\n" +
    "-----END CERTIFICATE-----\n";

  private String PLATFORM_CERT2 = "-----BEGIN CERTIFICATE-----\n" +
      "MIIDaDCCAlCgAwIBAgIGAXj0521PMA0GCSqGSIb3DQEBCwUAMDQxHDAaBgNVBAMM\n" +
      "E3liLWFkbWluLXRlc3QtYXJuYXYxFDASBgNVBAoMC2V4YW1wbGUuY29tMB4XDTIx\n" +
      "MDQyMTE0NDgzMFoXDTIyMDQyMTE0NDgzMFowEzERMA8GA1UEAwwIeXVnYWJ5dGUw\n" +
      "ggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQC+ZW8mVRXyvprX+ONwvn//\n" +
      "QffrPvPT43kkLF6YVrhm/KGDCM7LHBBRxskjZrc1w6eaok2gMhJPOpbea/gI5Rbj\n" +
      "RmhbUGw6/Qcu5PA+s7MzF2+Q9GO/EP1un+9GQp1zAM2/C/zE3yQdk3aAXeISlBnA\n" +
      "zndVZ4p7i18+EDzSJewF44mqW41mitogTXDDVMwBy+XroDG9tWq2D6Go1od6D0UR\n" +
      "ztbbfGaG2z4Ytpvt65b2GhVUhLRtBvc/UMDwcDazgwEY/aZToV4dBrWFsTQ6xl+A\n" +
      "vgjVtvgYoXQVtICbKGXsSad+i/A+KqcK/AX5VlRTel0w/zNOIVfExnrrRLnA8did\n" +
      "AgMBAAGjgaAwgZ0wDAYDVR0TAQH/BAIwADBhBgNVHSMEWjBYgBSZePsXT8pxsJrL\n" +
      "NmMDzf3cFMHGQ6E4pDYwNDEcMBoGA1UEAwwTeWItYWRtaW4tdGVzdC1hcm5hdjEU\n" +
      "MBIGA1UECgwLZXhhbXBsZS5jb22CBgF49OdsoDAdBgNVHQ4EFgQUn3Wg4rS8XPo3\n" +
      "4aYgAAVPHknKrYUwCwYDVR0PBAQDAgLkMA0GCSqGSIb3DQEBCwUAA4IBAQAPXYeZ\n" +
      "hv4Xq4exrFOCEpDgl+J8igSFmCVmTBzZj1PJaLhzmNinq0CPcLa+kZmjqYzkJ3jh\n" +
      "98R42CsYDlSbaYDBkZE4Bfus37y/XUQfQuy1MD8mpOelV0X07mPnn1/ZF53cA1Wr\n" +
      "tOVGcg7m56x9HEfjF1piKx7iOqLmFlSUkED1vMtMJdzJJ0AJi5UqLGqPc+se7BQF\n" +
      "FCD3UDRos2BoF1DprmPJCyGaG47hCEBQx3Tpc/q/53FGkdp+FhOH2c/6iwNjBuhq\n" +
      "QdjHUEqn2BmrQ8DsvLYKyDZbar98st0fLgmstFuX73C4QeN9aodxK9VaJhVC2i6e\n" +
      "JYH1CVA0eBgz47Dk\n" +
      "-----END CERTIFICATE-----\n";


  private String PLATFORM_KEY2 = "-----BEGIN RSA PRIVATE KEY-----\n" +
    "MIIEowIBAAKCAQEAvmVvJlUV8r6a1/jjcL5//0H36z7z0+N5JCxemFa4ZvyhgwjO\n" +
    "yxwQUcbJI2a3NcOnmqJNoDISTzqW3mv4COUW40ZoW1BsOv0HLuTwPrOzMxdvkPRj\n" +
    "vxD9bp/vRkKdcwDNvwv8xN8kHZN2gF3iEpQZwM53VWeKe4tfPhA80iXsBeOJqluN\n" +
    "ZoraIE1ww1TMAcvl66AxvbVqtg+hqNaHeg9FEc7W23xmhts+GLab7euW9hoVVIS0\n" +
    "bQb3P1DA8HA2s4MBGP2mU6FeHQa1hbE0OsZfgL4I1bb4GKF0FbSAmyhl7Emnfovw\n" +
    "PiqnCvwF+VZUU3pdMP8zTiFXxMZ660S5wPHYnQIDAQABAoIBACzKvD1uYv16rf8F\n" +
    "RKyvhHlO0b58TuyYZVWHQrHgJP3FjVHAbrYF4ij69TLo5U02vGV6rXx0iy4sgHXP\n" +
    "PMkK7DmHxOFGqE+wW1JC9eoqaIwqhUq61ASNQLIX2jjTTytRExZRAaRnQp3apVRJ\n" +
    "wffQ88YSTKzA8SljfoKhW02tMjuoBkbhw3NZV7quVHLGp/63lQvPFEiu8vwksP6S\n" +
    "87XheiOUZsjjeD7IoQPUXuYrKqRktXgvd3Kfie1tRBI11atnSHZO8h4bPWpfLuzT\n" +
    "ESoY7sgB744pjmw6d2LRwRuH9nmIdH0j3WlErFbniKHnBFZ+6WcL2F6cwY4DbxlA\n" +
    "1JRVC1kCgYEA3uXFSmvgsqRY0eowjzc0S4oQc6y4e3KLp7LSi5CMOti9jUoQpDxj\n" +
    "Du3StYcwsrBrmeA8i2Xd7j8UhGlVfLuM6IwD3HCsYhFLsTGiMkpyRR+9HHZleFEI\n" +
    "tiX7JvJ6YK2bIfjtO0Zb8jVe14U+su7MR6gRusRMVcvDM+kJQRpkwDcCgYEA2qwD\n" +
    "LZukwbOT7vXqHFuNR4ES4QnLbVYNCjY9mFdHEOnTDLWuBe5nDWMwGaHWzly9GJRP\n" +
    "3t+G3ySUA2s2F36ri9r9szik5RsHiif959npW67uRImsWKoPXfoy5HreRYK1mDEI\n" +
    "kI2j6GnSuGibscXb4Sqpq4FHaAIHaO+SFJNle8sCgYEAg3dGHBTwnKzubjEQnwfS\n" +
    "YlN2TKOs07LFyA1ivUpuSy8W7cSneBbd3ipLQyIPiPUhIcruKtHUSfcOpOJzt3Pv\n" +
    "MiGTDWuvYNAOst4xajQancaQhb8XLhWta7GPJnOR1n/OpgTp9zISfRMZc0h1qJfM\n" +
    "CN+KRXwOAfSCl5V1Dd8BWZUCgYBlVGpQmxw4yl9Vz0zCAUaxiMVX1LMYolR+k+3t\n" +
    "BxyeYMv0yseuZfAJCxqB9jXVALJ7jlaIn6ofAxM3llnFf2Q89ai0gR4gMGtJAQku\n" +
    "hBMX8jLlParIl11xnfwxViJjsZM2yBoA6jG3BDqqS4dCVL50U9G18L1HHBGU9dcK\n" +
    "g9b2CwKBgCumJ/04YXqww48o5yFAnMfKqbnMyUCGGuYgogqNntwsdoFjD6iRGBcV\n" +
    "n9sdS7qzRUbvBePsp3FdjNEDiRxcX1cft0xt1MDYpsLZ6VWehOXSuJlKj4HS+gXS\n" +
    "pbbOjZApJYRQkLX9CAzSotEcxXFSq2xnlPcKTcTNbfqrjPyFWvq4\n" +
    "-----END RSA PRIVATE KEY-----\n";


  @Test
  public void testUploadRootCA() {
    Calendar cal = Calendar.getInstance();
    Date certStart = cal.getTime();
    cal.add(Calendar.YEAR, 1);
    Date certExpiry = cal.getTime();
    UUID rootCA = null;
    CertificateInfo.Type type = CertificateInfo.Type.CustomCertHostPath;
    String cert_content = ROOT_CERT1;

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
      assertEquals("Unable to get cert Object.", e.getMessage());
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
    String cert_content = ROOT_CERT1;
    try {
      rootCA = CertificateHelper.uploadRootCA("test", c.uuid, "/tmp", cert_content, "test_key",
        certStart, certExpiry, type, null);
    } catch (Exception e) {
      assertEquals("Unable to get Private Key.", e.getMessage());
    }
  }

  @Test
  public void testUploadRootCAWithPlatformCert() {
    Calendar cal = Calendar.getInstance();
    Date certStart = cal.getTime();
    cal.add(Calendar.YEAR, 1);
    Date certExpiry = cal.getTime();
    UUID rootCA = null;
    CertificateInfo.Type type = CertificateInfo.Type.CustomCertHostPath;
    String cert_content = ROOT_CERT1;
    CertificateParams.CustomCertInfo customInfo = new CertificateParams.CustomCertInfo();
    customInfo.platformCertContent = PLATFORM_CERT1;
    customInfo.platformKeyContent = PLATFORM_KEY1;
    try {
      rootCA = CertificateHelper.uploadRootCA("test", c.uuid, "/tmp", cert_content, null,
          certStart, certExpiry, type, customInfo);
    } catch (Exception e) {
      fail(e.getMessage());
    }
    assertNotNull(CertificateInfo.get(rootCA));
    assertNotNull(CertificateInfo.get(rootCA).platformCert);
    assertNotNull(CertificateInfo.get(rootCA).platformKey);
  }

  @Test
  public void testUploadRootCAWithPlatformCertInvalidKey() {
    Calendar cal = Calendar.getInstance();
    Date certStart = cal.getTime();
    cal.add(Calendar.YEAR, 1);
    Date certExpiry = cal.getTime();
    UUID rootCA = null;
    CertificateInfo.Type type = CertificateInfo.Type.CustomCertHostPath;
    String cert_content = ROOT_CERT1;
    CertificateParams.CustomCertInfo customInfo = new CertificateParams.CustomCertInfo();
    customInfo.platformCertContent = PLATFORM_CERT1;
    customInfo.platformKeyContent = PLATFORM_KEY2;
    try {
      rootCA = CertificateHelper.uploadRootCA("test", c.uuid, "/tmp", cert_content, null,
          certStart, certExpiry, type, customInfo);
    } catch (Exception e) {
      assertEquals(e.getMessage(), "Invalid certificate.");
    }
  }

  @Test
  public void testUploadRootCAWithPlatformCertInvalidRoot() {
    Calendar cal = Calendar.getInstance();
    Date certStart = cal.getTime();
    cal.add(Calendar.YEAR, 1);
    Date certExpiry = cal.getTime();
    UUID rootCA = null;
    CertificateInfo.Type type = CertificateInfo.Type.CustomCertHostPath;
    String cert_content = ROOT_CERT2;
    CertificateParams.CustomCertInfo customInfo = new CertificateParams.CustomCertInfo();
    customInfo.platformCertContent = PLATFORM_CERT1;
    customInfo.platformKeyContent = PLATFORM_KEY1;
    try {
      rootCA = CertificateHelper.uploadRootCA("test", c.uuid, "/tmp", cert_content, null,
          certStart, certExpiry, type, customInfo);
    } catch (Exception e) {
      assertEquals(e.getMessage(), "Platform cert not signed by root cert.");
    }
  }
}
