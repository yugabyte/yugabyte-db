/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.ha;

import static com.yugabyte.yw.models.ScopedRuntimeConfig.GLOBAL_SCOPE_UUID;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertThrows;
import static play.test.Helpers.fakeRequest;

import com.yugabyte.yw.common.FakeApi;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.Users.Role;
import io.ebean.DB;
import io.ebean.Database;
import org.junit.Before;
import org.junit.Test;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.ws.WSClient;
import play.mvc.Http;

public class PlatformInstanceClientFactoryTest extends FakeDBApplication {

  private static final String KEY = "/api/customers/%s/runtime_config/%s/key/%s";
  private String authToken;
  private Customer customer;
  private Database localEBeanServer;
  private FakeApi fakeApi;
  private PlatformInstanceClientFactory platformInstanceClientFactory;

  private static final String REMOTE_ACME_ORG = "http://remote.acme.org";

  private static final String BAD_CA_CERT_KEY = "-----BAD CERT-----\n";

  private static final String GOOD_CA_CERT_KEY =
      "-----BEGIN CERTIFICATE-----\n"
          + "MIIDzTCCArWgAwIBAgIQCjeHZF5ftIwiTv0b7RQMPDANBgkqhkiG9w0BAQsFADBa\n"
          + "MQswCQYDVQQGEwJJRTESMBAGA1UEChMJQmFsdGltb3JlMRMwEQYDVQQLEwpDeWJl\n"
          + "clRydXN0MSIwIAYDVQQDExlCYWx0aW1vcmUgQ3liZXJUcnVzdCBSb290MB4XDTIw\n"
          + "MDEyNzEyNDgwOFoXDTI0MTIzMTIzNTk1OVowSjELMAkGA1UEBhMCVVMxGTAXBgNV\n"
          + "BAoTEENsb3VkZmxhcmUsIEluYy4xIDAeBgNVBAMTF0Nsb3VkZmxhcmUgSW5jIEVD\n"
          + "QyBDQS0zMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEua1NZpkUC0bsH4HRKlAe\n"
          + "nQMVLzQSfS2WuIg4m4Vfj7+7Te9hRsTJc9QkT+DuHM5ss1FxL2ruTAUJd9NyYqSb\n"
          + "16OCAWgwggFkMB0GA1UdDgQWBBSlzjfq67B1DpRniLRF+tkkEIeWHzAfBgNVHSME\n"
          + "GDAWgBTlnVkwgkdYzKz6CFQ2hns6tQRN8DAOBgNVHQ8BAf8EBAMCAYYwHQYDVR0l\n"
          + "BBYwFAYIKwYBBQUHAwEGCCsGAQUFBwMCMBIGA1UdEwEB/wQIMAYBAf8CAQAwNAYI\n"
          + "KwYBBQUHAQEEKDAmMCQGCCsGAQUFBzABhhhodHRwOi8vb2NzcC5kaWdpY2VydC5j\n"
          + "b20wOgYDVR0fBDMwMTAvoC2gK4YpaHR0cDovL2NybDMuZGlnaWNlcnQuY29tL09t\n"
          + "bmlyb290MjAyNS5jcmwwbQYDVR0gBGYwZDA3BglghkgBhv1sAQEwKjAoBggrBgEF\n"
          + "BQcCARYcaHR0cHM6Ly93d3cuZGlnaWNlcnQuY29tL0NQUzALBglghkgBhv1sAQIw\n"
          + "CAYGZ4EMAQIBMAgGBmeBDAECAjAIBgZngQwBAgMwDQYJKoZIhvcNAQELBQADggEB\n"
          + "AAUkHd0bsCrrmNaF4zlNXmtXnYJX/OvoMaJXkGUFvhZEOFp3ArnPEELG4ZKk40Un\n"
          + "+ABHLGioVplTVI+tnkDB0A+21w0LOEhsUCxJkAZbZB2LzEgwLt4I4ptJIsCSDBFe\n"
          + "lpKU1fwg3FZs5ZKTv3ocwDfjhUkV+ivhdDkYD7fa86JXWGBPzI6UAPxGezQxPk1H\n"
          + "goE6y/SJXQ7vTQ1unBuCJN0yJV0ReFEQPaA1IwQvZW+cwdFD19Ae8zFnWSfda9J1\n"
          + "CZMRJCQUzym+5iPDuI9yP+kHyCREU3qzuWFloUwOxkgAyXVjBYdwRVKD05WdRerw\n"
          + "6DEdfgkfCv4+3ao8XnTSrLE=\n"
          + "-----END CERTIFICATE-----";

  @Override
  protected GuiceApplicationBuilder configureApplication(GuiceApplicationBuilder builder) {
    return super.configureApplication(builder);
  }

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    Users user = ModelFactory.testUser(customer, Role.SuperAdmin);
    authToken = user.createAuthToken();
    localEBeanServer = DB.getDefault();
    fakeApi = new FakeApi(app, localEBeanServer);
    platformInstanceClientFactory = app.injector().instanceOf(PlatformInstanceClientFactory.class);
  }

  @Test
  public void getDefaultClient() {
    final PlatformInstanceClient platformInstanceClient =
        platformInstanceClientFactory.getClient("clusterK$Y", REMOTE_ACME_ORG, null);
    assertNotEquals(
        "Expect custom wsClient differnt from default",
        app.injector().instanceOf(WSClient.class),
        platformInstanceClient.getApiHelper().getWsClient());
  }

  @Test
  public void getCustomClient() {
    setWsConfig(GOOD_CA_CERT_KEY);
    final PlatformInstanceClient platformInstanceClient =
        platformInstanceClientFactory.getClient("clusterK$Y", REMOTE_ACME_ORG, null);
    assertNotEquals(
        "Expect custom wsClient differnt from default",
        app.injector().instanceOf(WSClient.class),
        platformInstanceClient.getApiHelper().getWsClient());

    // get client with same config
    final PlatformInstanceClient platformInstanceClient2 =
        platformInstanceClientFactory.getClient("clusterK$Y", REMOTE_ACME_ORG, null);

    // set new config
    setWsConfig(GOOD_CA_CERT_KEY);
    final PlatformInstanceClient platformInstanceClient3 =
        platformInstanceClientFactory.getClient("clusterK$Y", REMOTE_ACME_ORG, null);
    // This should NOT reuse the underlying apiHelper
    assertNotEquals(platformInstanceClient, platformInstanceClient2);
    assertNotEquals(
        platformInstanceClient.getApiHelper().getWsClient(),
        platformInstanceClient3.getApiHelper().getWsClient());
  }

  @Test
  public void setWsConfig_badCert() {
    assertThrows(RuntimeException.class, () -> setWsConfig(BAD_CA_CERT_KEY));
  }

  private void setWsConfig(String pemKey) {
    String wsConfig =
        String.format(
            "    {\n"
                + "      ssl {\n"
                + "        loose.acceptAnyCert = false\n"
                + "        trustManager {\n"
                + "          stores += {\n"
                + "            type = PEM\n"
                + "            data = \"\"\"%s\"\"\"\n"
                + "          }\n"
                + "        }\n"
                + "      }\n"
                + "    }\n",
            pemKey);

    setConfigKey("yb.ha.ws", wsConfig);
  }

  private void setConfigKey(String k, String v) {
    Http.RequestBuilder request =
        fakeRequest("PUT", String.format(KEY, customer.getUuid(), GLOBAL_SCOPE_UUID, k))
            .header("X-AUTH-TOKEN", authToken)
            .header("content-type", "text/plain")
            .bodyText(v);
    fakeApi.route(request);
  }
}
