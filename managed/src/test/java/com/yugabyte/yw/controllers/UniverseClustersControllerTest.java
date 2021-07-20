/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.FakeApiHelper.doRequestWithAuthToken;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import junitparams.JUnitParamsRunner;
import org.junit.runner.RunWith;
import play.libs.Json;
import play.mvc.Result;

@RunWith(JUnitParamsRunner.class)
public class UniverseClustersControllerTest extends UniverseCreateControllerTestBase {
  @Override
  protected String universeCreateUrl() {
    return "/api/customers/" + customer.uuid + "/universes/clusters";
  }

  @Override
  protected JsonNode getUniverseJson(Result universeCreateResponse) {
    String universeUUID =
        Json.parse(contentAsString(universeCreateResponse)).get("resourceUUID").asText();
    String url = "/api/customers/" + customer.uuid + "/universes/" + universeUUID;
    Result getResponse = doRequestWithAuthToken("GET", url, authToken);
    assertOk(getResponse);
    return Json.parse(contentAsString(getResponse));
  }
}
