/*
 * Copyright 2022 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common;

import static org.hamcrest.CoreMatchers.endsWith;
import static org.hamcrest.CoreMatchers.startsWith;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.mvc.Http.Status.NOT_FOUND;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.oracle.bmc.ServiceDetails;
import com.oracle.bmc.model.BmcException;
import com.yugabyte.yw.forms.PlatformResults.YBPError;
import java.util.UUID;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeoutException;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Result;

public class YWErrorHandlerTest extends FakeDBApplication {

  @Test
  public void onClientErrorNotFound()
      throws InterruptedException, ExecutionException, TimeoutException {
    final ObjectNode bodyJson = Json.newObject().put("name", "value");
    final UUID uuid = UUID.randomUUID();
    final UUID uuid1 = UUID.randomUUID();
    final Result result = createSupportBundleMalformedUri(bodyJson, uuid, uuid1);
    assertEquals(NOT_FOUND, result.status());
    YBPError json = Json.fromJson(Json.parse(contentAsString(result)), YBPError.class);
    assertFalse(json.success);
    assertEquals(json.httpMethod, "POST");
    assertThat(json.requestUri, endsWith("/support_bundle"));
    assertThat(json.error, startsWith("HTTP Client Error: 404(Not Found), details:"));
  }

  @Test
  public void ociErrorMappedToShortMessage() {
    ServiceDetails details =
        new ServiceDetails("Identity", "GetUser", "https://identity.oraclecloud.com/users/x", null);
    BmcException unauthorized =
        new BmcException(401, "NotAuthenticated", "Bad signature.", "req-id", details);
    PlatformServiceException mapped =
        YWErrorHandler.toPlatformServiceException(new RuntimeException(unauthorized)).get();
    // Not 401: the UI sends a 401 to the login page.
    assertEquals(BAD_REQUEST, mapped.getHttpStatus());
    assertEquals(
        "OCI Identity GetUser failed: Bad signature. (401 NotAuthenticated)",
        mapped.getUserVisibleMessage());

    BmcException serverError = new BmcException(503, "ServiceUnavailable", "Busy.", "req-id");
    mapped = YWErrorHandler.toPlatformServiceException(serverError).get();
    assertEquals(INTERNAL_SERVER_ERROR, mapped.getHttpStatus());
    assertEquals(
        "OCI request failed: Busy. (503 ServiceUnavailable). OCI request ID: req-id",
        mapped.getUserVisibleMessage());

    BmcException throttled = new BmcException(429, "TooManyRequests", "Slow down.", "req-id");
    mapped = YWErrorHandler.toPlatformServiceException(throttled).get();
    assertEquals(INTERNAL_SERVER_ERROR, mapped.getHttpStatus());
  }

  private Result createSupportBundleMalformedUri(ObjectNode bodyJson, UUID uuid, UUID uuid1)
      throws InterruptedException, ExecutionException, TimeoutException {
    String uri = "/api/%s/universes/%s/support_bundle";
    return routeWithYWErrHandler(
        fakeRequest("POST", String.format(uri, uuid.toString(), uuid1.toString()))
            .bodyJson(bodyJson));
  }
}
