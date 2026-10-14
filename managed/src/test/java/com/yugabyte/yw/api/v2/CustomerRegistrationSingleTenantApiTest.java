// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2;

import static com.yugabyte.yw.api.v2.CustomerRegistrationApiTest.register;
import static com.yugabyte.yw.api.v2.CustomerRegistrationApiTest.registerOk;
import static com.yugabyte.yw.api.v2.CustomerRegistrationApiTest.registrationSpec;
import static com.yugabyte.yw.common.AssertHelper.assertConflict;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static org.junit.Assert.assertEquals;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.models.Customer;
import org.junit.Test;
import play.mvc.Result;

public class CustomerRegistrationSingleTenantApiTest extends FakeDBApplication {

  @Test
  public void additionalRegistrationIsConflictEvenForSuperAdmin() {
    JsonNode first = registerOk(app, registrationSpec("admin@example.com"), null);

    Result result =
        assertPlatformException(
            () ->
                register(
                    app, registrationSpec("tenant@example.com"), first.get("api_token").asText()));

    assertConflict(result, "A customer is already registered on this single-tenant YBA instance");
    assertEquals(1, Customer.find.query().findCount());
  }

  @Test
  public void retryOfFirstRegistrationIsConflict() {
    registerOk(app, registrationSpec("admin@example.com"), null);

    Result result =
        assertPlatformException(() -> register(app, registrationSpec("admin@example.com"), null));

    assertConflict(result, "A customer is already registered on this single-tenant YBA instance");
  }
}
