// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2;

import static com.yugabyte.yw.api.v2.CustomerRegistrationApiTest.register;
import static com.yugabyte.yw.api.v2.CustomerRegistrationApiTest.registerOk;
import static com.yugabyte.yw.api.v2.CustomerRegistrationApiTest.registrationSpec;
import static com.yugabyte.yw.common.AssertHelper.assertConflict;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static org.junit.Assert.assertEquals;
import static play.mvc.Http.Status.CONFLICT;

import api.v2.handlers.CustomerManagementHandler;
import api.v2.models.CustomerRegistrationSpec;
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.Customer;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CyclicBarrier;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import org.junit.Test;
import play.mvc.Result;
import play.test.Helpers;

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

  @Test
  public void concurrentFirstRegistrationsCreateOneCustomer() throws Exception {
    CustomerManagementHandler handler = app.injector().instanceOf(CustomerManagementHandler.class);
    int callers = 2;
    CyclicBarrier barrier = new CyclicBarrier(callers);
    ExecutorService executor = Executors.newFixedThreadPool(callers);
    try {
      List<Future<Integer>> results = new ArrayList<>();
      for (int i = 0; i < callers; i++) {
        CustomerRegistrationSpec spec =
            new CustomerRegistrationSpec()
                .code("admin")
                .name("Example Customer")
                .email("admin" + i + "@example.com")
                .password("Passw0rd#123");
        results.add(
            executor.submit(
                () -> {
                  barrier.await();
                  try {
                    handler.registerCustomer(Helpers.fakeRequest().build(), spec);
                    return 200;
                  } catch (PlatformServiceException e) {
                    return e.getHttpStatus();
                  }
                }));
      }
      List<Integer> statuses = new ArrayList<>();
      for (Future<Integer> result : results) {
        statuses.add(result.get());
      }
      statuses.sort(null);
      assertEquals(List.of(200, CONFLICT), statuses);
      assertEquals(1, Customer.find.query().findCount());
    } finally {
      executor.shutdownNow();
    }
  }
}
