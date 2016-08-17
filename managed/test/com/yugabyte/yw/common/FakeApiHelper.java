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

  public static Result requestWithAuthToken(String method, String url) {
    Http.RequestBuilder request = Helpers.fakeRequest(method, url)
      .header("X-AUTH-TOKEN", getAuthToken());
    return Helpers.route(request);
  }

  public static Result requestWithAuthToken(String method, String url, JsonNode body) {
    Http.RequestBuilder request = Helpers.fakeRequest(method, url)
      .header("X-AUTH-TOKEN", getAuthToken())
      .bodyJson(body);
    return Helpers.route(request);
  }


}
