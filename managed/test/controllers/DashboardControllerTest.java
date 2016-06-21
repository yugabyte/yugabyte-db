// Copyright (c) YugaByte, Inc.

package controllers;

import helpers.FakeDBApplication;
import models.yb.Customer;
import org.hamcrest.CoreMatchers;
import org.junit.Before;
import org.junit.Test;
import play.mvc.Http;
import play.mvc.Result;

import java.util.Optional;

import static org.junit.Assert.*;
import static play.test.Helpers.*;

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
		Result result = route(fakeRequest(controllers.routes.DashboardController.index()).cookie(validCookie));
		assertEquals(OK, result.status());
		assertThat(contentAsString(result), CoreMatchers.containsString("body id=\"dashboard\""));
		assertThat(contentAsString(result), CoreMatchers.containsString("\"customerUUID\" value=\""+ customer.uuid + "\""));
	}

	@Test
	public void testDashboardIndexPageWithoutValidCookie() {
		Result result = route(fakeRequest(controllers.routes.DashboardController.index()));
		assertEquals(SEE_OTHER, result.status());
		assertTrue(result.redirectLocation().equals(Optional.of("/login")));
	}

	@Test
	public void testDashboardCreateInstancePage() {
		String authToken = customer.createAuthToken();
		Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
		Result result = route(fakeRequest(controllers.routes.DashboardController.createInstance()).cookie(validCookie));
		assertEquals(OK, result.status());
		assertThat(contentAsString(result), CoreMatchers.containsString("body id=\"create-instance\""));
		assertThat(contentAsString(result), CoreMatchers.containsString("\"customerUUID\" value=\""+ customer.uuid + "\""));
	}

	@Test
	public void testDashboardListInstancesPage() {
		String authToken = customer.createAuthToken();
		Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
		Result result = route(fakeRequest(controllers.routes.DashboardController.instances()).cookie(validCookie));
		assertEquals(OK, result.status());
		assertThat(contentAsString(result), CoreMatchers.containsString("body id=\"list-instance\""));
		assertThat(contentAsString(result), CoreMatchers.containsString("\"customerUUID\" value=\""+ customer.uuid + "\""));
	}

	@Test
	public void testDashboardEditProfilePage() {
		String authToken = customer.createAuthToken();
		Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
		Result result = route(fakeRequest(controllers.routes.DashboardController.profile()).cookie(validCookie));
		assertEquals(OK, result.status());
		assertThat(contentAsString(result), CoreMatchers.containsString("body id=\"edit-profile\""));
		assertThat(contentAsString(result), CoreMatchers.containsString("\"customerUUID\" value=\"" + customer.uuid + "\""));
	}
}
