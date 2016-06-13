package integration;

// Copyright (c) Yugabyte, Inc.

import models.yb.Customer;
import org.junit.*;
import static play.test.Helpers.*;
import static org.junit.Assert.*;
import play.inject.guice.GuiceApplicationBuilder;
import play.Application;
import java.util.Map;
import play.test.Helpers;
import play.test.WithApplication;

public class HomeControllerIntegrationTest  extends WithApplication {

	@Override
	protected Application provideApplication() {
		return new GuiceApplicationBuilder()
				.configure((Map) Helpers.inMemoryDatabase())
				.build();
	}

	@Before
	public void setUp() {
		Customer customer = Customer.create("Test Customer", "test@customer.com", "password");
		customer.save();
	}

  @Test
  public void testUnAuthorizedLandingPage() {
	  running(testServer(3333), HTMLUNIT, browser -> {
		  browser.goTo("http://localhost:3333");
		  assertTrue(browser.pageSource().contains("Please log in"));
	  });
  }

	// TODO: Add more integration test
}
