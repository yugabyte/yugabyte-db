// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.models.Users.Role;
import static play.test.Helpers.route;

import akka.stream.Materializer;
import akka.stream.javadsl.Source;
import akka.util.ByteString;
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.controllers.HAAuthenticator;
import com.yugabyte.yw.controllers.TokenAuthenticator;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.function.BiFunction;
import play.Application;
import play.libs.Files;
import play.mvc.Http;
import play.mvc.Result;
import play.test.Helpers;

public class FakeApiHelper {
  private static String getAuthToken() {
    Customer customer = Customer.find.query().where().eq("code", "tc").findOne();
    Users user;
    if (customer == null) {
      customer = Customer.create("vc", "Valid Customer");
      user = Users.create("foo@bar.com", "password", Role.Admin, customer.uuid, false);
    }
    user = Users.find.query().where().eq("customer_uuid", customer.uuid).findOne();
    return user.createAuthToken();
  }

  public static Result doRequest(String method, String url) {
    return doRequestWithAuthToken(method, url, getAuthToken());
  }

  public static Result doGetRequestNoAuth(String url) {
    Http.RequestBuilder request = Helpers.fakeRequest("GET", url);
    return route(request);
  }

  public static Result doRequestWithAuthToken(String method, String url, String authToken) {
    Http.RequestBuilder request =
        Helpers.fakeRequest(method, url).header(TokenAuthenticator.AUTH_TOKEN_HEADER, authToken);
    return route(request);
  }

  public static Result doRequestWithJWT(String method, String url, String authToken) {
    Http.RequestBuilder request =
        Helpers.fakeRequest(method, url).header(TokenAuthenticator.API_JWT_HEADER, authToken);
    return route(request);
  }

  public static Result doRequestWithCustomHeaders(
      String method, String url, Map<String, String> headers) {
    Http.RequestBuilder request = Helpers.fakeRequest(method, url);
    for (Map.Entry<String, String> headerEntry : headers.entrySet()) {
      request.header(headerEntry.getKey(), headerEntry.getValue());
    }
    return route(request);
  }

  public static Result doRequestWithHAToken(String method, String url, String haToken) {
    Http.RequestBuilder request =
        Helpers.fakeRequest(method, url)
            .header(HAAuthenticator.HA_CLUSTER_KEY_TOKEN_HEADER, haToken);
    return route(request);
  }

  public static Result doRequestWithHATokenAndBody(
      String method, String url, String haToken, JsonNode body) {
    Http.RequestBuilder request =
        Helpers.fakeRequest(method, url)
            .header(HAAuthenticator.HA_CLUSTER_KEY_TOKEN_HEADER, haToken)
            .bodyJson(body);
    return route(request);
  }

  public static Result doRequestWithBody(String method, String url, JsonNode body) {
    return doRequestWithAuthTokenAndBody(method, url, getAuthToken(), body);
  }

  public static Result doRequestWithAuthTokenAndBody(
      String method, String url, String authToken, JsonNode body) {
    Http.RequestBuilder request =
        Helpers.fakeRequest(method, url)
            .header(TokenAuthenticator.AUTH_TOKEN_HEADER, authToken)
            .bodyJson(body);
    return route(request);
  }

  public static Result doRequestWithBodyAndWithoutAuthToken(
      String method, String url, JsonNode body) {
    Http.RequestBuilder request = Helpers.fakeRequest(method, url).bodyJson(body);
    return route(request);
  }

  public static Result doRequestWithJWTAndBody(
      String method, String url, String authToken, JsonNode body) {
    Http.RequestBuilder request =
        Helpers.fakeRequest(method, url)
            .header(TokenAuthenticator.API_JWT_HEADER, authToken)
            .bodyJson(body);
    return route(request);
  }

  public static Result doRequestWithMultipartData(
      String method,
      String url,
      List<Http.MultipartFormData.Part<Source<ByteString, ?>>> data,
      Materializer mat) {
    return doRequestWithAuthTokenAndMultipartData(method, url, getAuthToken(), data, mat);
  }

  public static Result doRequestWithAuthTokenAndMultipartData(
      String method,
      String url,
      String authToken,
      List<Http.MultipartFormData.Part<Source<ByteString, ?>>> data,
      Materializer mat) {
    Http.RequestBuilder request =
        Helpers.fakeRequest(method, url)
            .header(TokenAuthenticator.AUTH_TOKEN_HEADER, authToken)
            .bodyMultipart(data, Files.singletonTemporaryFileCreator(), mat);
    return route(request);
  }

  /**
   * If you want to quickly fix existing test that returns YWError json when exception gets thrown
   * then use this function instead of Helpers.route(). Alternatively change the test to expect that
   * YWException get thrown
   */
  public static Result routeWithYWErrHandler(Http.RequestBuilder requestBuilder, Application app)
      throws InterruptedException, ExecutionException, TimeoutException {
    YWErrorHandler YWErrorHandler = app.injector().instanceOf(YWErrorHandler.class);
    CompletableFuture<Result> future =
        CompletableFuture.supplyAsync(() -> route(app, requestBuilder));
    BiFunction<Result, Throwable, CompletionStage<Result>> f =
        (result, throwable) -> {
          if (throwable == null) return CompletableFuture.supplyAsync(() -> result);
          return YWErrorHandler.onServerError(null, throwable);
        };

    return future.handleAsync(f).thenCompose(x -> x).get(20000, TimeUnit.MILLISECONDS);
  }
}
