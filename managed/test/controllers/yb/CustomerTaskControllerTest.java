// Copyright (c) YugaByte, Inc.

package controllers.yb;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import helpers.FakeDBApplication;
import models.yb.Customer;
import models.yb.CustomerTask;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

import java.util.Optional;
import java.util.UUID;

import static org.hamcrest.CoreMatchers.*;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.junit.Assert.*;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;
import static play.test.Helpers.route;

public class CustomerTaskControllerTest extends FakeDBApplication {
  private Customer customer;

  @Before
  public void setUp() { customer = Customer.create("Valid Customer", "foo@bar.com", "password"); }

  @Test
  public void testTaskHistoryEmptyListAsJSON() {
    String authToken = customer.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = route(fakeRequest("GET", "/api/customers/" + customer.uuid + "/tasks")
      .cookie(validCookie)
      .header("Accept", Http.MimeTypes.JSON)
    );

    assertEquals(OK, result.status());

    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.isArray());
    assertEquals(0, json.size());
  }

  @Test
  public void testTaskHistoryEmptyListAsHTML() {
    String authToken = customer.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = route(fakeRequest("GET", "/api/customers/" + customer.uuid + "/tasks")
      .cookie(validCookie)
      .header("Accept", Http.MimeTypes.HTML)
    );
    assertEquals(OK, result.status());
    assertThat(result.contentType(), is(equalTo(Optional.of(Http.MimeTypes.HTML))));
    assertThat(contentAsString(result).trim(), is(equalTo("")));
  }

  @Test
  public void testTaskHistoryEmptyListUnknownContentType() {
    String authToken = customer.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = route(fakeRequest("GET", "/api/customers/" + customer.uuid + "/tasks")
      .cookie(validCookie)
      .header("Accept", Http.MimeTypes.TEXT)
    );

    assertEquals(BAD_REQUEST, result.status());
  }

  @Test
  public void testTaskHistoryListAsJSON() {
    String authToken = customer.createAuthToken();
    UUID taskUUID = UUID.randomUUID();
    CustomerTask.create(customer, taskUUID, CustomerTask.TargetType.Instance, CustomerTask.TaskType.Create, "Foo");

    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = route(fakeRequest("GET", "/api/customers/" + customer.uuid + "/tasks")
      .cookie(validCookie)
      .header("Accept", Http.MimeTypes.JSON)
    );

    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.isArray());
    assertEquals(1, json.size());
    assertThat(json.get(0).get("taskUUID").asText(), allOf(notNullValue(), equalTo(taskUUID.toString())));
    assertThat(json.get(0).get("friendlyDescription").asText(), allOf(notNullValue(), equalTo("Creating Instance : Foo")));
  }

  @Test
  public void testTaskHistoryListAsHTML() {
    String authToken = customer.createAuthToken();
    UUID taskUUID = UUID.randomUUID();
    CustomerTask.create(customer, taskUUID, CustomerTask.TargetType.Instance, CustomerTask.TaskType.Create, "Foo");

    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = route(fakeRequest("GET", "/api/customers/" + customer.uuid + "/tasks")
      .cookie(validCookie)
      .header("Accept", Http.MimeTypes.HTML)
    );

    assertEquals(OK, result.status());
    assertThat(contentAsString(result), allOf(notNullValue(), containsString("Creating Instance : Foo")));

  }
}
