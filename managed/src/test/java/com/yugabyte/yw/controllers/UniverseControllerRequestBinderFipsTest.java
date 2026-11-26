// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.controllers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.PlatformServiceException;
import org.junit.Test;
import play.libs.Json;

// Pure checks: running a test app with yb.fips.enabled would swap the JVM-wide security providers.
public class UniverseControllerRequestBinderFipsTest {

  @Test
  public void testRejectsExplicitFipsDisabledOnFipsYba() {
    PlatformServiceException e =
        assertThrows(
            PlatformServiceException.class,
            () ->
                UniverseControllerRequestBinder.rejectFipsDisabledOnFipsYba(
                    Json.parse("{\"fipsEnabled\": false}"), true));
    assertEquals(BAD_REQUEST, e.getHttpStatus());
    assertTrue(e.getMessage().contains("FIPS mode"));
  }

  @Test
  public void testAllowsOmittedOrEnabledFipsOnFipsYba() {
    UniverseControllerRequestBinder.rejectFipsDisabledOnFipsYba(Json.parse("{}"), true);
    UniverseControllerRequestBinder.rejectFipsDisabledOnFipsYba(
        Json.parse("{\"fipsEnabled\": null}"), true);
    UniverseControllerRequestBinder.rejectFipsDisabledOnFipsYba(
        Json.parse("{\"fipsEnabled\": true}"), true);
    UniverseControllerRequestBinder.rejectFipsDisabledOnFipsYba(null, true);
  }

  @Test
  public void testAllowsFipsDisabledOnNonFipsYba() {
    UniverseControllerRequestBinder.rejectFipsDisabledOnFipsYba(
        Json.parse("{\"fipsEnabled\": false}"), false);
  }
}
