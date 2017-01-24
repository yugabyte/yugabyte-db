// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;


import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.models.Customer;
import play.mvc.Http;
import play.mvc.Result;
import play.test.Helpers;

public class FakeApiHelper {
  private static String getAuthToken() {
    Customer customer = Customer.create("Valid Customer", "foo@bar.com", "password");
    return customer.createAuthToken();
  }

  public static Result doRequest(String method, String url) {
    return doRequestWithAuthToken(method, url, getAuthToken());
  }

  public static Result doRequestWithAuthToken(String method, String url, String authToken) {
    Http.RequestBuilder request = Helpers.fakeRequest(method, url)
            .header("X-AUTH-TOKEN", authToken);
    return Helpers.route(request);
  }

  public static Result doRequestWithBody(String method, String url, JsonNode body) {
    return doRequestWithAuthTokenAndBody(method, url, getAuthToken(), body);
  }

  public static Result doRequestWithAuthTokenAndBody(String method, String url, String authToken, JsonNode body) {
    Http.RequestBuilder request = Helpers.fakeRequest(method, url)
            .header("X-AUTH-TOKEN", authToken)
            .bodyJson(body);
    return Helpers.route(request);
  }
}
