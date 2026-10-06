// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.helpers.telemetry;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.helpers.TelemetryProviderService;
import com.yugabyte.yw.models.helpers.telemetry.LokiConfig.ProbeVerdict;
import org.junit.Test;

public class LokiConfigTest {

  private LokiConfig config(String endpoint) {
    LokiConfig config = new LokiConfig();
    config.setEndpoint(endpoint);
    config.setAuthType(AuthCredentials.AuthType.NoAuth);
    return config;
  }

  // The probe GETs a POST-only path, so 405 - not 200 - is the healthy answer.
  @Test
  public void testClassifyProbeStatus() {
    assertEquals(ProbeVerdict.PATH_MISSING, LokiConfig.classifyProbeStatus(404));

    assertEquals(ProbeVerdict.AUTH_REJECTED, LokiConfig.classifyProbeStatus(401));
    assertEquals(ProbeVerdict.AUTH_REJECTED, LokiConfig.classifyProbeStatus(403));

    assertEquals(ProbeVerdict.UNHEALTHY, LokiConfig.classifyProbeStatus(429));
    assertEquals(ProbeVerdict.UNHEALTHY, LokiConfig.classifyProbeStatus(500));
    assertEquals(ProbeVerdict.UNHEALTHY, LokiConfig.classifyProbeStatus(502));
    assertEquals(ProbeVerdict.UNHEALTHY, LokiConfig.classifyProbeStatus(503));

    assertEquals(ProbeVerdict.SERVED, LokiConfig.classifyProbeStatus(200));
    assertEquals(ProbeVerdict.SERVED, LokiConfig.classifyProbeStatus(204));
    assertEquals(ProbeVerdict.SERVED, LokiConfig.classifyProbeStatus(400));
    assertEquals(ProbeVerdict.SERVED, LokiConfig.classifyProbeStatus(405));
    assertEquals(ProbeVerdict.SERVED, LokiConfig.classifyProbeStatus(415));
  }

  @Test
  public void testValidateConfigFieldsRequiresScheme() {
    PlatformServiceException e =
        assertThrows(
            PlatformServiceException.class,
            () -> config("loki.example.com:3100").validateConfigFields());
    assertTrue(e.getMessage(), e.getMessage().contains("http:// or https://"));
  }

  @Test
  public void testValidateConfigFieldsAcceptsBothSchemes() {
    LokiConfig http = config("http://loki.example.com:3100");
    http.validateConfigFields();
    assertEquals("http://loki.example.com:3100", http.getEndpoint());

    LokiConfig https = config("https://logs.example.com");
    https.validateConfigFields();
    assertEquals("https://logs.example.com", https.getEndpoint());
  }

  @Test
  public void testValidateConfigFieldsNormalizesEndpoint() {
    LokiConfig trailingSlash = config("http://loki.example.com:3100/");
    trailingSlash.validateConfigFields();
    assertEquals("http://loki.example.com:3100", trailingSlash.getEndpoint());

    LokiConfig pushPath =
        config("http://loki.example.com:3100" + TelemetryProviderService.LOKI_PUSH_ENDPOINT);
    pushPath.validateConfigFields();
    assertEquals("http://loki.example.com:3100", pushPath.getEndpoint());
  }

  // The exporter keeps a base path, so normalization must too.
  @Test
  public void testValidateConfigFieldsKeepsBasePath() {
    LokiConfig withBasePath = config("https://gateway.example.com/loki");
    withBasePath.validateConfigFields();
    assertEquals("https://gateway.example.com/loki", withBasePath.getEndpoint());
  }
}
