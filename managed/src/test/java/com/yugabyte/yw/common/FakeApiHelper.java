// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;


import akka.stream.Materializer;
import akka.stream.javadsl.Source;
import akka.util.ByteString;
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.models.Customer;
import play.mvc.Http;
import play.mvc.Result;
import play.test.Helpers;

import java.util.List;
import java.util.Map;

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

  public static Result doRequestWithMultipartData(String method, String url,
                                                  List<Http.MultipartFormData.Part<Source<ByteString, ?>>> data,
                                                  Materializer mat) {
    return doRequestWithAuthTokenAndMultipartData(method, url, getAuthToken(), data, mat);
  }

  public static Result doRequestWithAuthTokenAndMultipartData(
      String method, String url, String authToken,
      List<Http.MultipartFormData.Part<Source<ByteString, ?>>> data,
      Materializer mat) {
    Http.RequestBuilder request = Helpers.fakeRequest(method, url)
        .header("X-AUTH-TOKEN", authToken)
        .bodyMultipart(data, mat);
    return Helpers.route(request);
  }
}
