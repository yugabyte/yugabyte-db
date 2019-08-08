// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import play.mvc.Action;
import play.mvc.Http;
import play.mvc.Result;
import play.mvc.Results;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.UUID;

import com.google.inject.Inject;

import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.models.Customer;

import static com.yugabyte.yw.common.ConfigHelper.ConfigType.Security;

public class TokenAuthenticator extends Action.Simple {
  public static final String COOKIE_AUTH_TOKEN = "authToken";
  public static final String AUTH_TOKEN_HEADER =  "X-AUTH-TOKEN";
  public static final String COOKIE_API_TOKEN = "apiToken";
  public static final String API_TOKEN_HEADER = "X-AUTH-YW-API-TOKEN";

  @Inject
  ConfigHelper configHelper;

  @Override
  public CompletionStage<Result> call(Http.Context ctx) {
    String securityLevel = (String) configHelper.getConfig(Security).get("level");
    if (securityLevel == null || !securityLevel.equals("insecure")) {
      String path = ctx.request().path();
      Pattern pattern = Pattern.compile(".*/customers/([a-zA-Z0-9-]+)(/.*)?");
      Matcher matcher = pattern.matcher(path);
      UUID custUUID = null;
      if (matcher.find()) {
        custUUID = UUID.fromString(matcher.group(1));
      }
      String token = fetchToken(ctx, true);
      Customer cust = null;
      if (token != null) {
        cust = Customer.authWithApiToken(token);
      } else {
        token = fetchToken(ctx, false);
        cust = Customer.authWithToken(token);
      }

      // Some authenticated calls don't actually need to be authenticated
      // (e.g. /metadata/column_types). Only check auth_token is valid in that case.
      if (cust != null && (custUUID == null || custUUID.equals(cust.uuid))) {
        ctx.request().withUsername(cust.getEmail());
        ctx.args.put("customer", cust);
      } else {
        // Send Forbidden Response if Authentication Fails
        return CompletableFuture.completedFuture(Results.forbidden("Unable To Authenticate Customer"));
      }
    }
    return delegate.call(ctx);
  }

  private String fetchToken(Http.Context ctx, boolean isApiToken) {
    String header, cookie;
    if (isApiToken) {
      header = API_TOKEN_HEADER;
      cookie = COOKIE_API_TOKEN;
    } else {
      header = AUTH_TOKEN_HEADER;
      cookie = COOKIE_AUTH_TOKEN;
    }
    String[] headerValue = ctx.request().headers().get(header);
    Http.Cookie cookieValue = ctx.request().cookie(cookie);

    if ((headerValue != null) && (headerValue.length == 1)) {
      return headerValue[0];
    } else if (cookieValue != null) {
      // If we are accessing authenticated pages, the auth token would be in the cookie
      return cookieValue.value();
    }
    return null;
  }
}
