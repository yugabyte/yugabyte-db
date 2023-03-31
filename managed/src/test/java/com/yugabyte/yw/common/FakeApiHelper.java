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
      user = Users.create("foo@bar.com", "password", Role.Admin, customer.getUuid(), false);
    }
    user = Users.find.query().where().eq("customer_uuid", customer.getUuid()).findOne();
    return user.createAuthToken();
  }

  public static Result doRequest(Application app, String method, String url) {
    return doRequestWithAuthToken(app, method, url, getAuthToken());
  }

  public static Result doGetRequestNoAuth(Application app, String url) {
    Http.RequestBuilder request = Helpers.fakeRequest("GET", url);
    return route(app, request);
  }

  public static Result doRequestWithAuthToken(
      Application app, String method, String url, String authToken) {
    Http.RequestBuilder request =
        Helpers.fakeRequest(method, url).header(TokenAuthenticator.AUTH_TOKEN_HEADER, authToken);
    return route(app, request);
  }

  public static Result doRequestWithJWT(
      Application app, String method, String url, String authToken) {
    Http.RequestBuilder request =
        Helpers.fakeRequest(method, url).header(TokenAuthenticator.API_JWT_HEADER, authToken);
    return route(app, request);
  }

  public static Result doRequestWithCustomHeaders(
      Application app, String method, String url, Map<String, String> headers) {
    Http.RequestBuilder request = Helpers.fakeRequest(method, url);
    for (Map.Entry<String, String> headerEntry : headers.entrySet()) {
      request.header(headerEntry.getKey(), headerEntry.getValue());
    }
    return route(app, request);
  }

  public static Result doRequestWithHAToken(
      Application app, String method, String url, String haToken) {
    Http.RequestBuilder request =
        Helpers.fakeRequest(method, url)
            .header(HAAuthenticator.HA_CLUSTER_KEY_TOKEN_HEADER, haToken);
    return route(app, request);
  }

  public static Result doRequestWithHATokenAndBody(
      Application app, String method, String url, String haToken, JsonNode body) {
    Http.RequestBuilder request =
        Helpers.fakeRequest(method, url)
            .header(HAAuthenticator.HA_CLUSTER_KEY_TOKEN_HEADER, haToken)
            .bodyJson(body);
    return route(app, request);
  }

  public static Result doRequestWithBody(
      Application app, String method, String url, JsonNode body) {
    return doRequestWithAuthTokenAndBody(app, method, url, getAuthToken(), body);
  }

  public static Result doRequestWithAuthTokenAndBody(
      Application app, String method, String url, String authToken, JsonNode body) {
    Http.RequestBuilder request =
        Helpers.fakeRequest(method, url)
            .header(TokenAuthenticator.AUTH_TOKEN_HEADER, authToken)
            .bodyJson(body);
    return route(app, request);
  }

  public static Result doRequestWithBodyAndWithoutAuthToken(
      Application app, String method, String url, JsonNode body) {
    Http.RequestBuilder request = Helpers.fakeRequest(method, url).bodyJson(body);
    return route(app, request);
  }

  public static Result doRequestWithJWTAndBody(
      Application app, String method, String url, String authToken, JsonNode body) {
    Http.RequestBuilder request =
        Helpers.fakeRequest(method, url)
            .header(TokenAuthenticator.API_JWT_HEADER, authToken)
            .bodyJson(body);
    return route(app, request);
  }

  public static Result doRequestWithMultipartData(
      Application app,
      String method,
      String url,
      List<Http.MultipartFormData.Part<Source<ByteString, ?>>> data,
      Materializer mat) {
    return doRequestWithAuthTokenAndMultipartData(app, method, url, getAuthToken(), data, mat);
  }

  public static Result doRequestWithAuthTokenAndMultipartData(
      Application app,
      String method,
      String url,
      String authToken,
      List<Http.MultipartFormData.Part<Source<ByteString, ?>>> data,
      Materializer mat) {
    Http.RequestBuilder request =
        Helpers.fakeRequest(method, url)
            .header(TokenAuthenticator.AUTH_TOKEN_HEADER, authToken)
            .bodyMultipart(data, Files.singletonTemporaryFileCreator(), mat);
    return route(app, request);
  }

  /**
   * If you want to quickly fix existing test that returns YWError json when exception gets thrown
   * then use this function instead of Helpers.route(). Alternatively change the test to expect that
   * YWException get thrown
   */
  public static Result routeWithYWErrHandler(Application app, Http.RequestBuilder requestBuilder)
      throws InterruptedException, ExecutionException, TimeoutException {
    YWErrorHandler YWErrorHandler = app.injector().instanceOf(YWErrorHandler.class);
    CompletableFuture<Result> future =
        CompletableFuture.supplyAsync(() -> route(app, requestBuilder));
    BiFunction<Result, Throwable, CompletionStage<Result>> f =
        (result, throwable) -> {
          if (throwable == null) return CompletableFuture.supplyAsync(() -> result);
          return YWErrorHandler.onServerError(requestBuilder.build(), throwable);
        };

    return future.handleAsync(f).thenCompose(x -> x).get(20000, TimeUnit.MILLISECONDS);
  }
}
