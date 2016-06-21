// Copyright (c) Yugabyte, Inc.

package controllers.yb;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import helpers.FakeDBApplication;
import models.yb.Customer;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

import java.util.UUID;

import static org.hamcrest.CoreMatchers.*;
import static play.test.Helpers.*;
import static org.junit.Assert.*;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.fakeRequest;

public class CustomerControllerTest extends FakeDBApplication {
	private Customer customer;
  @Before
  public void setUp() {
		customer = Customer.create("Valid Customer", "foo@bar.com", "password");
  }

	@Test
	public void testCustomerGETWithValidUUID() {
		String authToken = customer.createAuthToken();
		Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
		Result result = route(fakeRequest("GET", "/api/customers/" + customer.uuid).cookie(validCookie));
		assertEquals(OK, result.status());
		JsonNode json = Json.parse(contentAsString(result));

		assertThat(json.get("uuid").asText(), allOf(notNullValue(), equalTo(customer.uuid.toString())));
		assertEquals(json.get("name").asText(), customer.name);
	}

	@Test
	public void testCustomerGETWithInvalidUUID() {
		String authToken = customer.createAuthToken();
		UUID invalidUUID = UUID.randomUUID();
		Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
		Result result = route(fakeRequest("GET", "/api/customers/" + invalidUUID).cookie(validCookie));
		assertEquals(BAD_REQUEST, result.status());
		JsonNode json = Json.parse(contentAsString(result));

		assertThat(json.get("error").asText(), is(containsString("Invalid Customer UUID:" + invalidUUID)));
	}

	@Test
	public void testCustomerPUTWithValidParams() {
		String authToken = customer.createAuthToken();
		Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
		ObjectNode params = Json.newObject();
		params.put("email", "foo@bar.com");
		params.put("name", "Test Customer");
		params.put("password", "new-password");

		Result result = route(fakeRequest("PUT", "/api/customers/" + customer.uuid).cookie(validCookie).bodyJson(params));
		assertEquals(OK, result.status());
		JsonNode json = Json.parse(contentAsString(result));
		assertThat(json.get("uuid").asText(), is(equalTo(customer.uuid.toString())));
		assertThat(json.get("name").asText(), is(equalTo("Test Customer")));
	}

	@Test
	public void testCustomerPUTWithInvalidParams() {
		String authToken = customer.createAuthToken();
		Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
		ObjectNode params = Json.newObject();
		params.put("password", "new-password");

		Result result = route(fakeRequest("PUT", "/api/customers/" + customer.uuid).cookie(validCookie).bodyJson(params));
		assertEquals(BAD_REQUEST, result.status());
		JsonNode json = Json.parse(contentAsString(result));

		assertThat(contentAsString(result), is(containsString("\"name\":[\"This field is required\"]")));
		assertThat(contentAsString(result), is(containsString("\"email\":[\"This field is required\"]")));
	}

	@Test
	public void testCustomerPUTWithInvalidUUID() {
		String authToken = customer.createAuthToken();
		UUID invalidUUID = UUID.randomUUID();
		Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
		Result result = route(fakeRequest("PUT", "/api/customers/" + invalidUUID).cookie(validCookie));
		assertEquals(BAD_REQUEST, result.status());
		JsonNode json = Json.parse(contentAsString(result));

		assertThat(json.get("error").asText(), is(containsString("Invalid Customer UUID:" + invalidUUID)));
	}
}
