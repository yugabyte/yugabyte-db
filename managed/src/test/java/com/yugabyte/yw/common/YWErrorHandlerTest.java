/*
 * Copyright 2022 YugaByte, Inc. and Contributors
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
import static play.mvc.Http.Status.NOT_FOUND;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;

import com.fasterxml.jackson.databind.node.ObjectNode;
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

  private Result createSupportBundleMalformedUri(ObjectNode bodyJson, UUID uuid, UUID uuid1)
      throws InterruptedException, ExecutionException, TimeoutException {
    String uri = "/api/%s/universes/%s/support_bundle";
    return routeWithYWErrHandler(
        fakeRequest("POST", String.format(uri, uuid.toString(), uuid1.toString()))
            .bodyJson(bodyJson));
  }
}
