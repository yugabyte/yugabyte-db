/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.controllers.HAAuthenticator;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.Users.Role;
import io.ebean.Database;
import io.ebean.MockHelper;
import java.util.List;
import org.apache.pekko.stream.Materializer;
import org.apache.pekko.stream.javadsl.Source;
import org.apache.pekko.util.ByteString;
import play.Application;
import play.libs.Files;
import play.mvc.Http;
import play.mvc.Result;
import play.test.Helpers;

public class FakeApi {
  private final String authToken;
  private final Application app;
  private final Database appEBeanServer;

  private static String getAuthToken() {
    Customer customer = Customer.find.query().where().eq("code", "tc").findOne();
    Users user;
    if (customer == null) {
      customer = Customer.create("vc", "Valid Customer");
      Users.create("foo@bar.com", "password", Role.Admin, customer.getUuid(), false);
    }
    user = Users.find.query().where().eq("customer_uuid", customer.getUuid()).findOne();
    return user.createAuthToken();
  }

  public FakeApi(Application app, Database remoteEBenServer) {
    this.app = app;
    this.appEBeanServer = remoteEBenServer;
    authToken = getAuthToken();
  }

  public Result doRequest(String method, String url) {
    return doRequestWithAuthToken(method, url, authToken);
  }

  public Result doRequestWithAuthToken(String method, String url, String authToken) {
    Http.RequestBuilder request =
        Helpers.fakeRequest(method, url).header("X-AUTH-TOKEN", authToken);
    return route(request);
  }

  public Result route(Http.RequestBuilder request) {
    Database currentDefaultServer = null;
    try {
      currentDefaultServer = MockHelper.mock(appEBeanServer, true);
      return Helpers.route(app, request);
    } finally {
      MockHelper.mock(currentDefaultServer, true);
    }
  }

  public Result doRequestWithHAToken(String method, String url, String haToken) {
    Http.RequestBuilder request =
        Helpers.fakeRequest(method, url)
            .header(HAAuthenticator.HA_CLUSTER_KEY_TOKEN_HEADER, haToken);
    return route(request);
  }

  public Result doRequestWithHATokenAndBody(
      String method, String url, String haToken, JsonNode body) {
    Http.RequestBuilder request =
        Helpers.fakeRequest(method, url)
            .header(HAAuthenticator.HA_CLUSTER_KEY_TOKEN_HEADER, haToken)
            .bodyJson(body);
    return route(request);
  }

  public Result doRequestWithBody(String method, String url, JsonNode body) {
    return doRequestWithAuthTokenAndBody(method, url, authToken, body);
  }

  public Result doRequestWithAuthTokenAndBody(
      String method, String url, String authToken, JsonNode body) {
    Http.RequestBuilder request =
        Helpers.fakeRequest(method, url).header("X-AUTH-TOKEN", authToken).bodyJson(body);
    return route(request);
  }

  public Result doRequestWithMultipartData(
      String method,
      String url,
      List<Http.MultipartFormData.Part<Source<ByteString, ?>>> data,
      Materializer mat) {
    return doRequestWithAuthTokenAndMultipartData(method, url, authToken, data, mat);
  }

  public Result doRequestWithAuthTokenAndMultipartData(
      String method,
      String url,
      String authToken,
      List<Http.MultipartFormData.Part<Source<ByteString, ?>>> data,
      Materializer mat) {
    Http.RequestBuilder request =
        Helpers.fakeRequest(method, url)
            .header("X-AUTH-TOKEN", authToken)
            .bodyMultipart(data, Files.singletonTemporaryFileCreator(), mat);
    return route(request);
  }
}
