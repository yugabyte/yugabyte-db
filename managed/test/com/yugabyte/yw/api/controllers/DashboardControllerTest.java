// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.api.controllers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static play.mvc.Http.Status.OK;
import static play.mvc.Http.Status.SEE_OTHER;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;
import static play.test.Helpers.route;

import java.util.Optional;

import org.hamcrest.CoreMatchers;
import org.junit.Before;
import org.junit.Test;

import com.yugabyte.yw.api.models.Customer;
import com.yugabyte.yw.common.FakeDBApplication;

import play.mvc.Http;
import play.mvc.Result;

public class DashboardControllerTest extends FakeDBApplication {
	Customer customer;

	@Before
	public void setUp() {
		customer = Customer.create("Valid Customer", "foo@bar.com", "password");
	}

	@Test
	public void testDashboardIndexPageWithValidCookie() {
		String authToken = customer.createAuthToken();
		Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
		Result result = route(fakeRequest("GET", "/").cookie(validCookie));
		assertEquals(OK, result.status());
		assertThat(contentAsString(result), CoreMatchers.containsString("body id=\"dashboard\""));
		assertThat(contentAsString(result),
		           CoreMatchers.containsString("\"customerUUID\" value=\""+ customer.uuid + "\""));
	}

	@Test
	public void testDashboardIndexPageWithoutValidCookie() {
		Result result = route(fakeRequest("GET", "/"));
		assertEquals(SEE_OTHER, result.status());
		assertTrue(result.redirectLocation().equals(Optional.of("/login")));
	}

	@Test
	public void testDashboardCreateInstancePage() {
		String authToken = customer.createAuthToken();
		Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
		Result result = route(fakeRequest("GET", "/createInstance").cookie(validCookie));
		assertEquals(OK, result.status());
		assertThat(contentAsString(result), CoreMatchers.containsString("body id=\"create-instance\""));
		assertThat(contentAsString(result),
		           CoreMatchers.containsString("\"customerUUID\" value=\""+ customer.uuid + "\""));
	}

	@Test
	public void testDashboardListInstancesPage() {
		String authToken = customer.createAuthToken();
		Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
		Result result = route(fakeRequest("GET", "/instances").cookie(validCookie));
		assertEquals(OK, result.status());
		assertThat(contentAsString(result), CoreMatchers.containsString("body id=\"list-instance\""));
		assertThat(contentAsString(result),
		           CoreMatchers.containsString("\"customerUUID\" value=\""+ customer.uuid + "\""));
	}

	@Test
	public void testDashboardEditProfilePage() {
		String authToken = customer.createAuthToken();
		Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
		Result result = route(fakeRequest("GET", "/profile").cookie(validCookie));
		assertEquals(OK, result.status());
		assertThat(contentAsString(result), CoreMatchers.containsString("body id=\"edit-profile\""));
		assertThat(contentAsString(result),
		           CoreMatchers.containsString("\"customerUUID\" value=\"" + customer.uuid + "\""));
	}
}
