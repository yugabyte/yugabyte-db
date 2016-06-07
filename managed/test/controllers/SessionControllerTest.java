// Copyright (c) Yugabyte, Inc.

package controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import models.Customer;
import org.junit.Before;
import org.junit.Test;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.mvc.Result;
import play.test.Helpers;
import play.test.WithApplication;
import java.util.Map;
import static play.test.Helpers.*;
import static org.junit.Assert.*;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.fakeRequest;

public class SessionControllerTest extends WithApplication {

    @Override
    protected Application provideApplication() {
        return new GuiceApplicationBuilder()
                .configure((Map) Helpers.inMemoryDatabase())
                .build();
    }

    @Before
    public void setUp() {
        Customer customer = Customer.create("Valid Customer", "foo@bar.com", "password");
        customer.save();
    }

    @Test
    public void testValidLogin() {
        ObjectNode loginJson = Json.newObject();
        loginJson.put("email", "Foo@bar.com");
        loginJson.put("password", "password");
        Result result = route(fakeRequest(controllers.routes.SessionController.login()).bodyJson(loginJson));
        JsonNode json = Json.parse(contentAsString(result));

        assertEquals(OK, result.status());
        assertNotNull(json.get("authToken"));
    }

    @Test
    public void testLoginWithInvalidPassword() {
        ObjectNode loginJson = Json.newObject();
        loginJson.put("email", "foo@bar.com");
        loginJson.put("password", "password1");
        Result result = route(fakeRequest(controllers.routes.SessionController.login()).bodyJson(loginJson));

        assertEquals(UNAUTHORIZED, result.status());
    }

    @Test
    public void testLoginWithNullPassword() {
        ObjectNode loginJson = Json.newObject();
        loginJson.put("email", "foo@bar.com");
        Result result = route(fakeRequest(controllers.routes.SessionController.login()).bodyJson(loginJson));

        assertEquals(BAD_REQUEST, result.status());
    }

    @Test
    public void testRegisterCustomer() {
        ObjectNode registerJson = Json.newObject();
        registerJson.put("email", "foo2@bar.com");
        registerJson.put("password", "password");
        registerJson.put("name", "Foo");

        Result result = route(fakeRequest(controllers.routes.SessionController.register()).bodyJson(registerJson));
        JsonNode json = Json.parse(contentAsString(result));

        assertEquals(OK, result.status());
        assertNotNull(json.get("authToken"));

        ObjectNode loginJson = Json.newObject();
        loginJson.put("email", "foo2@bar.com");
        loginJson.put("password", "password");
        result = route(fakeRequest(controllers.routes.SessionController.login()).bodyJson(loginJson));
        json = Json.parse(contentAsString(result));

        assertEquals(OK, result.status());
        assertNotNull(json.get("authToken"));
    }

    @Test
    public void testRegisterCustomerWithoutEmail() {
        ObjectNode registerJson = Json.newObject();
        registerJson.put("email", "foo@bar.com");
        Result result = route(fakeRequest(controllers.routes.SessionController.login()).bodyJson(registerJson));
        JsonNode json = Json.parse(contentAsString(result));

        assertEquals(BAD_REQUEST, result.status());
    }

    @Test
    public void testLogout() {
        ObjectNode loginJson = Json.newObject();
        loginJson.put("email", "Foo@bar.com");
        loginJson.put("password", "password");
        Result result = route(fakeRequest(controllers.routes.SessionController.login()).bodyJson(loginJson));
        JsonNode json = Json.parse(contentAsString(result));

        assertEquals(OK, result.status());
        String authToken = json.get("authToken").asText();
        result = route(fakeRequest(controllers.routes.SessionController.logout()).header("X-AUTH-TOKEN", authToken));
        assertTrue(result.redirectLocation().isPresent());
    }
}
