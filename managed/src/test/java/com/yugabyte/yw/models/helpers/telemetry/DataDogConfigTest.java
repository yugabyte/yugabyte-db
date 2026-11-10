// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.helpers.telemetry;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyMap;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.FORBIDDEN;

import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import java.util.Collections;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;

public class DataDogConfigTest {

  private static final String API_KEY = "test-api-key";

  private ApiHelper apiHelper;
  private RuntimeConfGetter confGetter;

  @Before
  public void setUp() {
    apiHelper = mock(ApiHelper.class);
    confGetter = mock(RuntimeConfGetter.class);
    skipConnectivityValidation(false);
  }

  private void skipConnectivityValidation(boolean skip) {
    when(confGetter.getGlobalConf(GlobalConfKeys.telemetrySkipConnectivityValidations))
        .thenReturn(skip);
  }

  private DataDogConfig config(String site) {
    DataDogConfig config = new DataDogConfig();
    config.setSite(site);
    config.setApiKey(API_KEY);
    return config;
  }

  private void assertRejected(DataDogConfig config) {
    PlatformServiceException e =
        assertThrows(PlatformServiceException.class, () -> config.validate(apiHelper, confGetter));
    assertEquals(BAD_REQUEST, e.getHttpStatus());
  }

  @Test
  public void testValidKeySendsKeyToSiteApiHost() {
    when(apiHelper.getRequest(anyString(), anyMap())).thenReturn(Json.parse("{\"valid\": true}"));

    config("us5.datadoghq.com").validate(apiHelper, confGetter);

    verify(apiHelper)
        .getRequest(
            "https://api.us5.datadoghq.com/api/v1/validate",
            Collections.singletonMap("DD-API-KEY", API_KEY));
  }

  @Test
  public void testAcceptsDatadogSites() {
    when(apiHelper.getRequest(anyString(), anyMap())).thenReturn(Json.parse("{\"valid\": true}"));
    for (String site :
        new String[] {
          "datadoghq.com",
          "us3.datadoghq.com",
          "us5.datadoghq.com",
          "ap1.datadoghq.com",
          "ap2.datadoghq.com",
          "datadoghq.eu",
          "ddog-gov.com",
          "DatadogHQ.com"
        }) {
      config(site).validate(apiHelper, confGetter);
    }
  }

  @Test
  public void testRejectsNonDatadogSiteWithoutSendingKey() {
    for (String site :
        new String[] {
          null,
          "",
          "example.com",
          "datadoghq.com.example.com",
          "evil-datadoghq.com",
          "https://datadoghq.com",
          "datadoghq.com/path",
          "datadoghq.com:8443",
          "a.b.datadoghq.com"
        }) {
      assertRejected(config(site));
    }
    verify(apiHelper, never()).getRequest(anyString(), any());
  }

  @Test
  public void testSkippedConnectivityStillRejectsNonDatadogSite() {
    skipConnectivityValidation(true);
    assertRejected(config("example.com"));

    DataDogConfig noKey = config("datadoghq.com");
    noKey.setApiKey(null);
    assertRejected(noKey);

    config("datadoghq.eu").validate(apiHelper, confGetter);
    verify(apiHelper, never()).getRequest(anyString(), any());
  }

  @Test
  public void testRejectsMissingApiKey() {
    DataDogConfig config = config("datadoghq.com");
    config.setApiKey(" ");
    assertRejected(config);
    verify(apiHelper, never()).getRequest(anyString(), any());
  }

  @Test
  public void testInvalidKeyResponseIsRejected() {
    when(apiHelper.getRequest(anyString(), anyMap()))
        .thenReturn(Json.parse("{\"errors\": [\"Forbidden\"]}"));
    assertRejected(config("datadoghq.com"));

    when(apiHelper.getRequest(anyString(), anyMap())).thenReturn(Json.parse("{\"valid\": false}"));
    assertRejected(config("datadoghq.com"));
  }

  @Test
  public void testRequestFailureIsRejected() {
    when(apiHelper.getRequest(anyString(), anyMap()))
        .thenThrow(new PlatformServiceException(FORBIDDEN, "HTTP request failed with status 403"));
    assertRejected(config("datadoghq.com"));
  }
}
