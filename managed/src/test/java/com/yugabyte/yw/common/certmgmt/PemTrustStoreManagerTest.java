/*
 * Copyright 2023 YugaByte, Inc. and Contributors
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
import static org.junit.Assert.fail;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.certmgmt.castore.PemTrustStoreManager;
import com.yugabyte.yw.models.CustomCaCertificateInfo;
import com.yugabyte.yw.models.Customer;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.security.KeyStoreException;
import java.security.cert.Certificate;
import java.security.cert.CertificateException;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnitRunner;

@Slf4j
@RunWith(MockitoJUnitRunner.class)
public class PemTrustStoreManagerTest extends FakeDBApplication {

  static String TMP_PEM_STORE = "/tmp/certmgmt/";
  static String PEM_TRUST_STORE_FILE;

  @InjectMocks PemTrustStoreManager pemTrustStoreManager;
  private Customer defaultCustomer;

  @Before
  public void setUp() throws IOException {
    defaultCustomer = ModelFactory.testCustomer();
    TMP_PEM_STORE += defaultCustomer.getUuid().toString();
    PEM_TRUST_STORE_FILE = TMP_PEM_STORE + "/" + PemTrustStoreManager.TRUSTSTORE_FILE_NAME;
    new File(TMP_PEM_STORE).mkdirs();
    new File(PEM_TRUST_STORE_FILE).createNewFile();
  }

  @After
  public void tearDown() throws IOException {
    FileUtils.deleteDirectory(new File(TMP_PEM_STORE));
  }

  public String getCertificateChain2Content() {
    String certChain =
        "-----BEGIN CERTIFICATE-----\n"
            + "MIICQzCCAawCCQDhN1VLJiS5JDANBgkqhkiG9w0BAQsFADBmMQswCQYDVQQGEwJJ\n"
            + "TjELMAkGA1UECAwCS0ExEjAQBgNVBAcMCUJlbmdhbHVydTELMAkGA1UECgwCWUIx\n"
            + "DDAKBgNVBAsMA0RldjEbMBkGA1UEAwwSQ2xpZW50IENlcnRpZmljYXRlMB4XDTIz\n"
            + "MTAwNjExMTM0NFoXDTMzMTAwMzExMTM0NFowZjELMAkGA1UEBhMCSU4xCzAJBgNV\n"
            + "BAgMAktBMRIwEAYDVQQHDAlCZW5nYWx1cnUxCzAJBgNVBAoMAllCMQwwCgYDVQQL\n"
            + "DANEZXYxGzAZBgNVBAMMEkNsaWVudCBDZXJ0aWZpY2F0ZTCBnzANBgkqhkiG9w0B\n"
            + "AQEFAAOBjQAwgYkCgYEA037hWIlJsttsV10ZIGDi0bzUdjouSirqhvjNxUZUEo0t\n"
            + "CNupAdjp5M+vyJj2xnAI8DziUslpYovFpPtuwPCuB4MzqO95xVLGLhszH9cQidua\n"
            + "Qu+cgFN9JqgrmHUuQJOkTZlPYAEwkE2lqFBvdhXgqICmbgOXgFVHpa8YB8f4U8UC\n"
            + "AwEAATANBgkqhkiG9w0BAQsFAAOBgQBmr3fX12cDXaqLMDOqvs8VsAKS7YdOS4K8\n"
            + "5RxupmOaoiw0pV2RMY0KSl6VXgcROBlGWo7WCUVVMXq6xw1mrV/RigoV8T+jZ6SQ\n"
            + "MvDXgw80Ykm1o+U5tvL6xQ33jTZrlPUDEEHjMq8SRmSzBbLs/G34cuFbnFFCm8h4\n"
            + "m4ZyRf8Nlw==\n"
            + "-----END CERTIFICATE-----\n"
            + "\n"
            + "-----BEGIN CERTIFICATE-----\n"
            + "MIIDFTCCAn6gAwIBAgICEAEwDQYJKoZIhvcNAQELBQAwZjELMAkGA1UEBhMCSU4x\n"
            + "CzAJBgNVBAgMAktBMRIwEAYDVQQHDAlCZW5nYWx1cnUxCzAJBgNVBAoMAllCMQww\n"
            + "CgYDVQQLDANEZXYxGzAZBgNVBAMMEkNsaWVudCBDZXJ0aWZpY2F0ZTAeFw0yMzEw\n"
            + "MDYxMTE0MDFaFw0yNDEwMDUxMTE0MDFaMGYxCzAJBgNVBAYTAklOMQswCQYDVQQI\n"
            + "DAJLQTESMBAGA1UEBwwJQmVuZ2FsdXJ1MQswCQYDVQQKDAJZQjEMMAoGA1UECwwD\n"
            + "RGV2MRswGQYDVQQDDBJDbGllbnQgQ2VydGlmaWNhdGUwgZ8wDQYJKoZIhvcNAQEB\n"
            + "BQADgY0AMIGJAoGBALUPV0LrhSq7cDPJuQnt/9k26rZuhDLYsy8mjP4yHV+S3T4x\n"
            + "TmVi3Q9VTQc16WBjGp31SLci9ZpCC79GdhmG8TuzzXe89udmvyfS24Tqxl/2jyZm\n"
            + "ABUHx/v/6Uvd3Q/GkUTkoyFAPb69Ra9ZbCCNIY7p08zM2U3ILsNJMJRJZnefAgMB\n"
            + "AAGjgdEwgc4wDAYDVR0TBAUwAwEB/zALBgNVHQ8EBAMCAaYwHQYDVR0OBBYEFBTQ\n"
            + "4JZlqLnFw7mZpgEl7UBlujG1MIGABgNVHSMEeTB3oWqkaDBmMQswCQYDVQQGEwJJ\n"
            + "TjELMAkGA1UECAwCS0ExEjAQBgNVBAcMCUJlbmdhbHVydTELMAkGA1UECgwCWUIx\n"
            + "DDAKBgNVBAsMA0RldjEbMBkGA1UEAwwSQ2xpZW50IENlcnRpZmljYXRlggkA4TdV\n"
            + "SyYkuSQwDwYDVR0RBAgwBocEChcQETANBgkqhkiG9w0BAQsFAAOBgQBlBoNDnIfT\n"
            + "nGw0TJSsR5MXXgnckHldUlsuA+T3UWlzG8KDlVY4F2pGm0fvPqN6YPpABjeB+ue9\n"
            + "W7SYPqMflpbEqAHTnC8poID91x7xx9FQzLqwOx8/fmsNZSXscJoPWRxOvqPMoEuh\n"
            + "iIHq0hzJqeG6abe076ILpUX7xhVZ9OZ6dQ==\n"
            + "-----END CERTIFICATE-----\n";

    return certChain;
  }

  public void addCertificateToYBA(String certPath, String certContent) throws IOException {
    File file = new File(certPath);
    file.createNewFile();
    FileWriter writer = new FileWriter(file);
    writer.write(certContent);
    writer.close();
  }

  public String getCertificateChain1Content() {
    String certificates =
        "-----BEGIN CERTIFICATE-----\n"
            + "MIICQzCCAawCCQDhN1VLJiS5JDANBgkqhkiG9w0BAQsFADBmMQswCQYDVQQGEwJJ\n"
            + "TjELMAkGA1UECAwCS0ExEjAQBgNVBAcMCUJlbmdhbHVydTELMAkGA1UECgwCWUIx\n"
            + "DDAKBgNVBAsMA0RldjEbMBkGA1UEAwwSQ2xpZW50IENlcnRpZmljYXRlMB4XDTIz\n"
            + "MTAwNjExMTM0NFoXDTMzMTAwMzExMTM0NFowZjELMAkGA1UEBhMCSU4xCzAJBgNV\n"
            + "BAgMAktBMRIwEAYDVQQHDAlCZW5nYWx1cnUxCzAJBgNVBAoMAllCMQwwCgYDVQQL\n"
            + "DANEZXYxGzAZBgNVBAMMEkNsaWVudCBDZXJ0aWZpY2F0ZTCBnzANBgkqhkiG9w0B\n"
            + "AQEFAAOBjQAwgYkCgYEA037hWIlJsttsV10ZIGDi0bzUdjouSirqhvjNxUZUEo0t\n"
            + "CNupAdjp5M+vyJj2xnAI8DziUslpYovFpPtuwPCuB4MzqO95xVLGLhszH9cQidua\n"
            + "Qu+cgFN9JqgrmHUuQJOkTZlPYAEwkE2lqFBvdhXgqICmbgOXgFVHpa8YB8f4U8UC\n"
            + "AwEAATANBgkqhkiG9w0BAQsFAAOBgQBmr3fX12cDXaqLMDOqvs8VsAKS7YdOS4K8\n"
            + "5RxupmOaoiw0pV2RMY0KSl6VXgcROBlGWo7WCUVVMXq6xw1mrV/RigoV8T+jZ6SQ\n"
            + "MvDXgw80Ykm1o+U5tvL6xQ33jTZrlPUDEEHjMq8SRmSzBbLs/G34cuFbnFFCm8h4\n"
            + "m4ZyRf8Nlw==\n"
            + "-----END CERTIFICATE-----\n"
            + "-----BEGIN CERTIFICATE-----\n"
            + "MIIDFTCCAn6gAwIBAgICEAIwDQYJKoZIhvcNAQELBQAwZjELMAkGA1UEBhMCSU4x\n"
            + "CzAJBgNVBAgMAktBMRIwEAYDVQQHDAlCZW5nYWx1cnUxCzAJBgNVBAoMAllCMQww\n"
            + "CgYDVQQLDANEZXYxGzAZBgNVBAMMEkNsaWVudCBDZXJ0aWZpY2F0ZTAeFw0yMzEw\n"
            + "MTAwNTIzMzdaFw0yNDEwMDkwNTIzMzdaMGYxCzAJBgNVBAYTAklOMQswCQYDVQQI\n"
            + "DAJLQTESMBAGA1UEBwwJQmVuZ2FsdXJ1MQswCQYDVQQKDAJZQjEMMAoGA1UECwwD\n"
            + "RGV2MRswGQYDVQQDDBJDbGllbnQgQ2VydGlmaWNhdGUwgZ8wDQYJKoZIhvcNAQEB\n"
            + "BQADgY0AMIGJAoGBAPacLQJ5kyDi37F8WAVrBgcyx+UvLR9hdmWhq+h4AyXO/Ibq\n"
            + "vpocAQRXoRC8eguXDSwNNVCYzz1QqyxyYY29XHiy5Fk2ZrMdg0bHArNoOWEcxQe0\n"
            + "kmH6y7yG7Y07FE11GVMMyzs/gaVv1NQGjt8IhZ7ZPpkx9X6/uoV07emWwFZNAgMB\n"
            + "AAGjgdEwgc4wDAYDVR0TBAUwAwEB/zALBgNVHQ8EBAMCAaYwHQYDVR0OBBYEFNPK\n"
            + "51Qbc/iCTki7ZECdR8vqoSiwMIGABgNVHSMEeTB3oWqkaDBmMQswCQYDVQQGEwJJ\n"
            + "TjELMAkGA1UECAwCS0ExEjAQBgNVBAcMCUJlbmdhbHVydTELMAkGA1UECgwCWUIx\n"
            + "DDAKBgNVBAsMA0RldjEbMBkGA1UEAwwSQ2xpZW50IENlcnRpZmljYXRlggkA4TdV\n"
            + "SyYkuSQwDwYDVR0RBAgwBocEChcQETANBgkqhkiG9w0BAQsFAAOBgQDOg9EyCo6c\n"
            + "Lg1+RDsztb+MBEYzL+X1ADNFEICLW7w7uboMow/CFIeT1wr0jdJj52a28ypJxX7v\n"
            + "gN4HytXW/d7uoi0Z7XWpDliZ1/+o+71vNKIxqWh00bGqFbxYMl0Pt08YWy4RcAYQ\n"
            + "TKb1mt19gJKGdjJDQz6lDSg20OqehrZmsQ==\n"
            + "-----END CERTIFICATE-----\n";

    return certificates;
  }

  public String getCertificateContent() {
    String certificate =
        "-----BEGIN CERTIFICATE-----\n"
            + "MIIDBTCCAe2gAwIBAgIQKvGH5iWg47RCxMcw/B3HfDANBgkqhkiG9w0BAQsFADAV\n"
            + "MRMwEQYDVQQDEwppdGVzdC1sZGFwMB4XDTIzMDIxMzA5MDkzMloXDTMzMDIxMzA5\n"
            + "MTkzMlowFTETMBEGA1UEAxMKaXRlc3QtbGRhcDCCASIwDQYJKoZIhvcNAQEBBQAD\n"
            + "ggEPADCCAQoCggEBAM0GyAjTfFqpN9+bTiUBo4sY3v3yhyc6cfbS8K8Pt3t2D7nW\n"
            + "0VnmLZOc1h7XmHv9aq67c5mjiC4q1O1zbVRh/3MofyzLywmCxfZSt8DjynN+FAxA\n"
            + "g46fa+8NdQMAKbmRWG+05j1AhY6LZJr6h2J126n45BjZGZCsfnrSnHyAlIe8ZTvA\n"
            + "H17WG0GKoCC84H43MYyunyaXmQZ7ImR7+lAhtIXtg9l7mNLHZTjZTI40iFxQV4T9\n"
            + "M+Antrhs5E5HGoaKagHqbxk2U+sW9KFn1+bo9UpgUlVZDtKkHsLzXZO/ilXXhgyL\n"
            + "HxJlxP8ed0qEoaLg3wFK3GJrzFmy4p33BXZBVzUCAwEAAaNRME8wCwYDVR0PBAQD\n"
            + "AgGGMA8GA1UdEwEB/wQFMAMBAf8wHQYDVR0OBBYEFAd4BgRooit2x4eJeukQXi+u\n"
            + "no3XMBAGCSsGAQQBgjcVAQQDAgEAMA0GCSqGSIb3DQEBCwUAA4IBAQCZA6FfNJKo\n"
            + "GlbxErYUSQc0BmlVTDP3TxriCKPzPaxl6WZ/p3bFkg8HvXLOySzl/kEAEe4KWe56\n"
            + "751Iz0vuGRihPqQgkikjfPtvIuLVSytHx19a/sDTZUCQM7qyhWHtYyL7FTxtXMDG\n"
            + "8nyrBRax1CNEDW/l0uwyxndopshFxaPvlLsz2SLVcP8g07DWpT9m0d+PnjHBI1Vl\n"
            + "B9LKHhpy27wUjYCctLO4BzEP9URJGGgpSI4YEoJIx3HHCSZcjtFxc9zHSeDU7EdY\n"
            + "1JKjULlmEPHyte03hkKh+8Ylcn/jo1UsVgbmnJpvQPhWGeI76TtnoM33LgZpnPLW\n"
            + "IgS49841p/6U\n"
            + "-----END CERTIFICATE-----\n";

    return certificate;
  }

  public String getCertificateChain3Content() {
    String certChain =
        "-----BEGIN CERTIFICATE-----\n"
            + "MIIFZDCCA0ygAwIBAgIIDrYWUG22FM8wDQYJKoZIhvcNAQELBQAwMzESMBAGA1UE\n"
            + "AwwJRklDQSBSb290MRAwDgYDVQQKDAdGTVIgTExDMQswCQYDVQQGEwJVUzAeFw0x\n"
            + "NjA3MTkyMjAyNTVaFw00NjA3MTkyMjAyNTVaMDMxEjAQBgNVBAMMCUZJQ0EgUm9v\n"
            + "dDEQMA4GA1UECgwHRk1SIExMQzELMAkGA1UEBhMCVVMwggIiMA0GCSqGSIb3DQEB\n"
            + "AQUAA4ICDwAwggIKAoICAQDBQ6PhzyH9OqT05jxb7oVP0n+XsHqw3FUw7OhuVqTs\n"
            + "FAYlMX8wVIsjsS/ozXMV26337yl5S7x62j9/e5KxGT6b2J1Xqfok2NUnx3pMk9/o\n"
            + "Dl9+ifrUF0RPw94bxVJCx3DhAjDxiSPJs9XXlJ8+tSUzPpr+ZK+PvyvunWwB3Jr2\n"
            + "ZBalKYQfSa6DWz5pIjcf+UwvMaW+NUMewOhcrWP55DTfVH3vfxh6OQkr/xWHYIYq\n"
            + "TFEBg4ctEW6vYiPi7SEn27wDVRHn1E1Tno9Eq/a+2INQq1JLVMV7NnkDAx+r+Rbw\n"
            + "h11lj6rkW3CKCpSv6Pu6EO03BSdResi0VzHCoa2EgrStSmjJq+DeVRQt8Bz0bP0D\n"
            + "ap/u16lxKZCRzs0LE8LDKMa4D/vRZfvPAnmsWwDRvW5JieUbBLWGO7AungJqirWb\n"
            + "EGqfHXVQfMWsFADROnWJpUJfvlVZ5DC4Cf9ewc8NZb5SZM0yml8KMngoPBJ1vpe4\n"
            + "dgWGGeygzTEHn15Ux9GKrXQ2xye4mQIK2okFuOIQZSPJDqcR3k6Rl+Ilm5KGfqO3\n"
            + "vpiKSdvmlcNhwGqKNUhDt8yUMr/9zSJV6Hxx8StAzqoX4f5RWnRiH90uSqTTS+Ui\n"
            + "QVPiQoSAvwkDgADYGbj8EBZhppqgCUw14I91vF8KUGQc2HcHYCjl8tP/PB8biupz\n"
            + "5wIDAQABo3wwejAPBgNVHRMBAf8EBTADAQH/MB8GA1UdIwQYMBaAFN6nbJ4lvQ8K\n"
            + "vWTSssDiysj/k3zEMBcGA1UdIAQQMA4wDAYKKwYBBAGKFB4BATAdBgNVHQ4EFgQU\n"
            + "3qdsniW9Dwq9ZNKywOLKyP+TfMQwDgYDVR0PAQH/BAQDAgGGMA0GCSqGSIb3DQEB\n"
            + "CwUAA4ICAQBrx1a+3wzbfqai4xrctuXkqxyAYl0BIYAli9uZA1f9+q92nOZ2ZFc9\n"
            + "6BzZGZeUtxlMd5mAVW+zC2lSKn9xWfZWA7rrpCPiMprXfJdQGA+HZ/w2hNtruCPv\n"
            + "tSZ/DkiQFz2DBYEnQOdPvYWZGCIcdA2IEtd/fLgSbENXBr2hoX0vHR7YnV3OExk6\n"
            + "9IJjYij1CY3IGXt0Zy1eI2APYuTGogAcYwhMvK/XDhNa2oYf0T++7UbzXzkq3Zy0\n"
            + "KxnOd1X3bsXjdZYGgTPPjycLTzbLxvbz14YlnoGHD0filMtfHHH0wG1lDieIU+lk\n"
            + "OL8Ff8Bk71khoP9qNgkS8CkjazUaW2PczjL4yczs+jvNB69h718Sg4xS7sV8/VC9\n"
            + "lmeT/glZNFhTZKNbK3LSMoumk4ElUw7MeWCuKqPD4OB5WHKMrcj0WtnZJYgagaco\n"
            + "RfpRSrd7CI9iD9yu2P173En8uzgMZA2/pMDf8gH6m3b5sz5td7nzdTaXVYQjkYBq\n"
            + "t8bZFHvD3hgsIeEcQ9v9XunEYVpCac6LcUDxL33c3+e4s419LrLkgQMnoFYBBezy\n"
            + "0TR5XPOoWin1SVgZ29WlIz432VJpw4mnrKdW5k4LcbdHRQ9E4ORNEJC2OesRAyBW\n"
            + "C5MpsYpIhsgPPL1y7HS9r83MXWReheDyOEl5SnNYMjrcOib086vCag==\n"
            + "-----END CERTIFICATE-----\n"
            + "\n"
            + "-----BEGIN CERTIFICATE-----\n"
            + "MIIF0TCCA7mgAwIBAgIIalPEPqScMu8wDQYJKoZIhvcNAQELBQAwMzESMBAGA1UE\n"
            + "AwwJRklDQSBSb290MRAwDgYDVQQKDAdGTVIgTExDMQswCQYDVQQGEwJVUzAeFw0x\n"
            + "NjA3MTkyMjE3MDhaFw00MTA3MTkyMjE3MDhaMDsxGjAYBgNVBAMMEUZJQ0EgSW50\n"
            + "ZXJtZWRpYXRlMRAwDgYDVQQKDAdGTVIgTExDMQswCQYDVQQGEwJVUzCCAiIwDQYJ\n"
            + "KoZIhvcNAQEBBQADggIPADCCAgoCggIBAMGGwmwlY9u9t6vESH4ehQCjge1mArZ+\n"
            + "ubHkcezLlZ0jY/yMDvh2BF/VASMVMrzih84lEvdQbJYz23wXVjKTmEEZuf64LDiL\n"
            + "5l8a4kMRbbeYK5Uq8KgqAY9CAfzIKNl01YR2AxzALdJ8qEptjHZYXuj3/tOSyaKz\n"
            + "g5APSLeLg+CbSVTa6gzmizEFpBuI/aWuw5RIWd2+T7gBv5Tpno2wh2cYSBos7/HF\n"
            + "zhJiG8ufDjo/mWro/VAq0j8NVbHELKrf61kJhwZv7Z8mIx3RXEFVdQLayFAitzPl\n"
            + "9Ul2kI3fOqNJgN73XILj75CjpSLxRaiciVQa6ux0Pqd3FlaQDN43BCb57BEuOREI\n"
            + "/XnxucktgwyDuBqM/POScRv6uWclTqLBIbrCaZj63/WVt3K+t3HmBWgGyX4z+N4/\n"
            + "NNWncfs61aPC974Ztbh2GOReqmijIDAOND33iY4hZOZFNTiy1d8wz2sOBVOwpl5D\n"
            + "e/A9/plepmblcuo3IcC6QRcFCiquD7SJ6e7UIM30bHFKyNHhvFL5k/hPZnCFEdyH\n"
            + "wKKAUY8u9iIHTt5l/L7oxsAd8myP5kB49XaF+W/t2VWWOKlaMrOq78y3BSZzIrMb\n"
            + "wK9aR4j6KCUBJahPDjAT/Q8qUsyebOR/fRGHy0un0TUw/sfZBYdF6VHHx1TnFrUr\n"
            + "3eyy2IL2LCtvAgMBAAGjgeAwgd0wDwYDVR0TAQH/BAUwAwEB/zAfBgNVHSMEGDAW\n"
            + "gBTep2yeJb0PCr1k0rLA4srI/5N8xDA/BggrBgEFBQcBAQQzMDEwLwYIKwYBBQUH\n"
            + "MAGGI2h0dHA6Ly9lamJjYXZhbC5mbXIuY29tL3N0YXR1cy9vY3NwMDkGA1UdHwQy\n"
            + "MDAwLqAsoCqGKGh0dHA6Ly9lamJjYXZhbC5mbXIuY29tL2NybC9maWNhcm9vdC5j\n"
            + "cmwwHQYDVR0OBBYEFGVBj6AkbmRQWylNcjIALL7ypX45MA4GA1UdDwEB/wQEAwIB\n"
            + "hjANBgkqhkiG9w0BAQsFAAOCAgEAmDZ2764ea1coc4ZOCobea9fUBfHCKI47IM9l\n"
            + "VDkN+PlGmFCEbkBfuqxsuPHH26z2whsEwsIMr2V5Qf1bf1+yXl0Sx86o1Y8WI4AU\n"
            + "AEDXnnRmF4E+YudHHbA7jzPamj3rq4+boNUtNRFhK3wW3WH8wfYhsCc9ZTz8wlFG\n"
            + "kN9wTQy6HM9R+9ly+hkh9A8Tv8R+Fxvc5gwIjeURZEC19RjHfAa5lJ7VHz8Y6ZYQ\n"
            + "lJo/Li2Pxo79e7mBbuBuU4FPRVCYaTEhaZTULD99eNu6pzPNluk/FIYAu/YJhhUX\n"
            + "cR7xUnO8ZRRWWmYaYJSo418iAe4qhQvs4G3YRdENZEneE3l6MjiNq+GKZt/KnUMi\n"
            + "yqIKM19l8pZw5onNuN44XbnGeNWMPRpS4tB2AOyuKlOZUDU7VV4mptD/parwTDLB\n"
            + "dWDi+wadPpEUmZTQkG+yMg4ubOEnu81rmBBOm3CeSsJypAKVgx3sTAUqC//jfZkW\n"
            + "9meeBnbn/ol/NKg+dUdMb0T8eef4S50jpNbXpy4jwTmKRRCm+ByxmGQYtADoy5c8\n"
            + "UgNuzLFhezAjfnl0570Bw0qK8B8mZmW8srDdpNwMRTk+LdtEaLlv2pgdA0SbTXl+\n"
            + "lv+ntTIhRNMh7fnk5EEzGfhv+HPOTBIV2Q9uy/KmxFSxPE2xvTajKxGk7lXJA1Bi\n"
            + "LIqYphQ=\n"
            + "-----END CERTIFICATE-----\n"
            + "\n"
            + "-----BEGIN CERTIFICATE-----\n"
            + "MIIF1jCCA76gAwIBAgIIIv77qJI0sdUwDQYJKoZIhvcNAQELBQAwOzEaMBgGA1UE\n"
            + "AwwRRklDQSBJbnRlcm1lZGlhdGUxEDAOBgNVBAoMB0ZNUiBMTEMxCzAJBgNVBAYT\n"
            + "AlVTMB4XDTE2MDcyODIxMjY1MVoXDTM2MDcyODIxMjY1MVowOTEYMBYGA1UEAwwP\n"
            + "RklDQSBJc3N1aW5nIEUxMRAwDgYDVQQKDAdGTVIgTExDMQswCQYDVQQGEwJVUzCC\n"
            + "AiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAOsnvfnZ8QICs84YDxo0DxJV\n"
            + "2ZNY7CXGVOgSk+LBJdA0U9ebSenSJWUtTNznCHt5w8t/8IzTQoVuo6HfKoHynzMw\n"
            + "Pe20rPQAS7Ot62ZchqrkZsnbcEjcYY1fBDWiIRqeFj53V0kCTXFq8qi9AszB/Nyu\n"
            + "vt0tZMJ4Sl6TzM9B076IRtGrLtpkWMJ3TBjnpPLUKwbDqv0vg78prPEx8DoL9Dmv\n"
            + "5gF8vjuyXS5KFvOREj3CAw6rW2cW0hzZV1nItAorxoeH9T3SzniH3KLMcm7KmJOi\n"
            + "24xzZivETJzYypYphd6eaZ3BZg+k9CyLbbWB2QVX1/rFZAZj0cQrkkr1JqYBBvrW\n"
            + "Vb1jU2zUGPlisYX+ecGZ91a2MTSGIPZPCkbq/HtwCOualwhwJV2Olu1o3PrPquI5\n"
            + "FV6Q60y+icKC27Y/CKOlt4DAcNY8WK/Kgtn1IEL2AX82wlEIPnY4YtH2RHj3EG/e\n"
            + "Xy9L89CITLX8EwvpkkW9KwZDT84rgrT6GVc3vD7bOMsBNhF7Lwj4Kofnc1ay/xaG\n"
            + "zWphyIWBrgfD+nc+bl7zBVzILSBsnRZiMhtJfjiEDY3FiOKKuO8kn606jMntF4VW\n"
            + "L8tISmTfSMEKIUU4nUjlSYPeVaOTdQnp5g6wU6YQ3soh2i97umtWPXCMnMz51VM5\n"
            + "JxL3mIuWy4NxOaU8V6f3AgMBAAGjgd8wgdwwDwYDVR0TAQH/BAUwAwEB/zAfBgNV\n"
            + "HSMEGDAWgBRlQY+gJG5kUFspTXIyACy+8qV+OTA/BggrBgEFBQcBAQQzMDEwLwYI\n"
            + "KwYBBQUHMAGGI2h0dHA6Ly9lamJjYXZhbC5mbXIuY29tL3N0YXR1cy9vY3NwMDgG\n"
            + "A1UdHwQxMC8wLaAroCmGJ2h0dHA6Ly9lamJjYXZhbC5mbXIuY29tL2NybC9maWNh\n"
            + "aW50LmNybDAdBgNVHQ4EFgQUAfJmhr3/kwKCRJoLd04vP8EaiU8wDgYDVR0PAQH/\n"
            + "BAQDAgGGMA0GCSqGSIb3DQEBCwUAA4ICAQBT7580KPs/Xg6t5NiAnerWb6KnlHPk\n"
            + "Gr2VgO4pmEDl1tBb/bbSWRQ1ByrYQspu+ZQqo5n4A4WkTYVQvhX8bqiDepig633S\n"
            + "gybX2q51RWYWgKph8cbvYAtHattqLUhlmkNqMepT/3Wx7vIACAmWloZNWB20EynI\n"
            + "fJUm0p1cCnJoJLaWHikq70IZX1ly3M0oSyaz5ezEHQXgpPbezQXDdtQxdoEVsrnu\n"
            + "iuccjGIMyFsQwCFFia7tmx+adwrfG1/yKhvn/L6+pcMGA47fPGIppDYT47dlILJk\n"
            + "zavVaik1JaoEOyMMJN6iJsypSgey7GV6KfXyclBeBU1RO0A5gPfg5pD8aZ6QPnnZ\n"
            + "K1WiiCSi2vcbdLExB0rS/I/J5xBtrMGRknfYkB+VQ2Hly8+5LP1lf0cThybO0Xal\n"
            + "v0Zejsg8zGXiHD5T62clfn4vrtt5D1227hc58lol1ZgdTNUd2nRwWahOTlkj3lib\n"
            + "WVBbCHlAH7sVhNSX3bd0KPXzy8CWnwrwbJMeqp6lKVwjcORve4RWaugyrwkqvtXa\n"
            + "dxFODwJU6TEk11yV1y+tnX4BngJouz4PoiSopdqRr92tlK2fu9lMkCmMjCZ/38tR\n"
            + "37eaos6RAqVKLWGfYkwwMumwVJt889LuYgC8sKNt9pPyj/LVoHoIddUJs3Jjhfi8\n"
            + "iDUYpcRmFIuvRQ==\n"
            + "-----END CERTIFICATE-----\n";

    return certChain;
  }

  @Test
  public void addCertificateChainToTrustStore()
      throws KeyStoreException, CertificateException, IOException, PlatformServiceException {
    // Add a new certificate to the YBA's PEM trust store.
    UUID certUUID = UUID.randomUUID();
    String certDirectory = TMP_PEM_STORE + certUUID.toString();
    new File(certDirectory).mkdirs();
    String certPath = certDirectory + "/ca.crt";
    String certificateContent = getCertificateChain1Content();
    addCertificateToYBA(certPath, certificateContent);

    pemTrustStoreManager.addCertificate(certPath, "test-cert", TMP_PEM_STORE, null, false);

    List<Certificate> pemTrustStoreCertificates =
        pemTrustStoreManager.getCertsInTrustStore(PEM_TRUST_STORE_FILE, null);
    assertEquals(2, pemTrustStoreCertificates.size());

    certUUID = UUID.randomUUID();
    certDirectory = TMP_PEM_STORE + certUUID.toString();
    new File(certDirectory).mkdirs();
    certPath = certDirectory + "/ca.crt";
    certificateContent = getCertificateChain2Content();
    addCertificateToYBA(certPath, certificateContent);
    pemTrustStoreManager.addCertificate(certPath, "test-cert-1", TMP_PEM_STORE, null, false);
    pemTrustStoreCertificates =
        pemTrustStoreManager.getCertsInTrustStore(PEM_TRUST_STORE_FILE, null);
    assertEquals(3, pemTrustStoreCertificates.size());
  }

  @Test
  public void addCertificateToTrustStore()
      throws KeyStoreException, CertificateException, IOException, PlatformServiceException {
    UUID certUUID = UUID.randomUUID();
    String certDirectory = TMP_PEM_STORE + certUUID.toString();
    new File(certDirectory).mkdirs();
    String certPath = certDirectory + "/ca.crt";

    String certificateContent = getCertificateContent();
    addCertificateToYBA(certPath, certificateContent);
    pemTrustStoreManager.addCertificate(certPath, "test-cert", TMP_PEM_STORE, null, false);

    List<Certificate> pemTrustStoreCertificates =
        pemTrustStoreManager.getCertsInTrustStore(PEM_TRUST_STORE_FILE, null);
    assertEquals(1, pemTrustStoreCertificates.size());
  }

  @Test
  public void removeCertificateChainFromTrustStore()
      throws KeyStoreException, CertificateException, IOException, PlatformServiceException {
    UUID certUUID = UUID.randomUUID();
    String certDirectory = TMP_PEM_STORE + certUUID.toString();
    new File(certDirectory).mkdirs();
    String certPath = certDirectory + "/ca.crt";

    String certificateContent = getCertificateChain1Content();
    CustomCaCertificateInfo.create(
        defaultCustomer.getUuid(), certUUID, "test-cert", certPath, new Date(), new Date(), true);
    addCertificateToYBA(certPath, certificateContent);
    pemTrustStoreManager.addCertificate(certPath, "test-cert", TMP_PEM_STORE, null, false);

    List<Certificate> pemTrustStoreCertificates =
        pemTrustStoreManager.getCertsInTrustStore(PEM_TRUST_STORE_FILE, null);
    assertEquals(2, pemTrustStoreCertificates.size());

    certUUID = UUID.randomUUID();
    certDirectory = TMP_PEM_STORE + certUUID.toString();
    new File(certDirectory).mkdirs();
    certPath = certDirectory + "/ca.crt";
    certificateContent = getCertificateChain2Content();
    CustomCaCertificateInfo.create(
        defaultCustomer.getUuid(), certUUID, "test-cert-1", certPath, new Date(), new Date(), true);
    addCertificateToYBA(certPath, certificateContent);

    pemTrustStoreManager.addCertificate(certPath, "test-cert-1", TMP_PEM_STORE, null, false);
    pemTrustStoreCertificates =
        pemTrustStoreManager.getCertsInTrustStore(PEM_TRUST_STORE_FILE, null);
    assertEquals(3, pemTrustStoreCertificates.size());

    pemTrustStoreManager.remove(certPath, "test-cert", TMP_PEM_STORE, null, false);
    pemTrustStoreCertificates =
        pemTrustStoreManager.getCertsInTrustStore(PEM_TRUST_STORE_FILE, null);
    assertEquals(2, pemTrustStoreCertificates.size());
  }

  @Test
  public void replaceCertificateChainInTrustStore()
      throws KeyStoreException, CertificateException, IOException, PlatformServiceException {
    UUID certUUID = UUID.randomUUID();
    String certDirectory = TMP_PEM_STORE + certUUID.toString();
    new File(certDirectory).mkdirs();
    String oldCertPath = certDirectory + "/ca.crt";

    String certificateContent = getCertificateChain1Content();
    CustomCaCertificateInfo.create(
        defaultCustomer.getUuid(),
        certUUID,
        "test-cert",
        oldCertPath,
        new Date(),
        new Date(),
        true);
    addCertificateToYBA(oldCertPath, certificateContent);
    pemTrustStoreManager.addCertificate(oldCertPath, "test-cert", TMP_PEM_STORE, null, false);

    List<Certificate> pemTrustStoreCertificates =
        pemTrustStoreManager.getCertsInTrustStore(PEM_TRUST_STORE_FILE, null);
    assertEquals(2, pemTrustStoreCertificates.size());

    certUUID = UUID.randomUUID();
    certDirectory = TMP_PEM_STORE + certUUID.toString();
    new File(certDirectory).mkdirs();
    String certPath = certDirectory + "/ca.crt";
    certificateContent = getCertificateChain2Content();
    CustomCaCertificateInfo.create(
        defaultCustomer.getUuid(), certUUID, "test-cert-1", certPath, new Date(), new Date(), true);
    addCertificateToYBA(certPath, certificateContent);

    pemTrustStoreManager.addCertificate(certPath, "test-cert-1", TMP_PEM_STORE, null, false);
    pemTrustStoreCertificates =
        pemTrustStoreManager.getCertsInTrustStore(PEM_TRUST_STORE_FILE, null);
    assertEquals(3, pemTrustStoreCertificates.size());

    certUUID = UUID.randomUUID();
    certDirectory = TMP_PEM_STORE + certUUID.toString();
    new File(certDirectory).mkdirs();
    String newCertPath = certDirectory + "/ca.crt";
    certificateContent = getCertificateChain3Content();
    addCertificateToYBA(newCertPath, certificateContent);

    pemTrustStoreManager.replaceCertificate(
        certPath, newCertPath, "test-cert-1", TMP_PEM_STORE, null, false);
    CustomCaCertificateInfo.create(
        defaultCustomer.getUuid(),
        certUUID,
        "test-cert-1",
        newCertPath,
        new Date(),
        new Date(),
        true);

    pemTrustStoreCertificates =
        pemTrustStoreManager.getCertsInTrustStore(PEM_TRUST_STORE_FILE, null);
    List<Certificate> allCerts = pemTrustStoreManager.getX509Certificate(newCertPath);
    allCerts.addAll(pemTrustStoreManager.getX509Certificate(oldCertPath));
    assertEquals(5, allCerts.size());
    for (Certificate cert : allCerts) {
      if (!pemTrustStoreCertificates.contains(cert)) {
        fail();
      }
    }
    assertEquals(5, pemTrustStoreCertificates.size());
  }
}
