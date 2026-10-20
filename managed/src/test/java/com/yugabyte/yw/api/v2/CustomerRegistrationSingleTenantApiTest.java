// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2;

import static com.yugabyte.yw.api.v2.CustomerRegistrationApiTest.register;
import static com.yugabyte.yw.api.v2.CustomerRegistrationApiTest.registerOk;
import static com.yugabyte.yw.api.v2.CustomerRegistrationApiTest.registrationSpec;
import static com.yugabyte.yw.common.AssertHelper.assertConflict;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static play.mvc.Http.Status.CONFLICT;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

import api.v2.handlers.CustomerManagementHandler;
import api.v2.models.CustomerRegistration;
import api.v2.models.CustomerRegistrationSpec;
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.controllers.RequestContext;
import com.yugabyte.yw.controllers.SessionController;
import com.yugabyte.yw.controllers.TokenAuthenticator;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.extended.UserWithFeatures;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CyclicBarrier;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import org.junit.Test;
import play.libs.Json;
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

  @Test
  public void firstRegistrationIsAttributedToTheNewUserEvenWithAStaleUserOnTheThread() {
    CustomerManagementHandler handler = app.injector().instanceOf(CustomerManagementHandler.class);
    Users stale = new Users();
    RequestContext.put(TokenAuthenticator.USER, new UserWithFeatures().setUser(stale));
    try {
      CustomerRegistration registration =
          handler.registerCustomer(
              Helpers.fakeRequest().build(),
              new CustomerRegistrationSpec()
                  .code("admin")
                  .name("Example Customer")
                  .email("admin@example.com")
                  .password("Passw0rd#123"));

      assertEquals(
          registration.getUserUuid(),
          RequestContext.get(TokenAuthenticator.USER).getUser().getUuid());
    } finally {
      RequestContext.clean(Set.of(TokenAuthenticator.USER));
    }
  }

  @Test
  public void v1FirstRegistrationIsAttributedToTheNewUserAndLeavesTheThreadAlone() {
    SessionController controller = app.injector().instanceOf(SessionController.class);
    RequestContext.put(TokenAuthenticator.USER, new UserWithFeatures().setUser(new Users()));
    JsonNode registration;
    try {
      Result result =
          controller.register(
              false,
              Helpers.fakeRequest("POST", "/api/v1/register")
                  .bodyJson(registrationSpec("admin@example.com"))
                  .build());
      assertEquals(contentAsString(result), OK, result.status());
      registration = Json.parse(contentAsString(result));
      // /register isn't wrapped in TokenAuthenticator, so it must clear the user itself.
      assertNull(RequestContext.getIfPresent(TokenAuthenticator.USER));
    } finally {
      RequestContext.clean(Set.of(TokenAuthenticator.USER));
    }

    List<Audit> entries = Audit.getAll(UUID.fromString(registration.get("customerUUID").asText()));
    assertEquals(1, entries.size());
    assertEquals(registration.get("userUUID").asText(), entries.get(0).getUserUUID().toString());
  }
}
