// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import play.mvc.Action;
import play.mvc.Http;
import play.mvc.Result;
import play.mvc.Results;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;

import com.yugabyte.yw.models.Customer;

public class TokenAuthenticator extends Action.Simple {
  public static final String COOKIE_AUTH_TOKEN = "authToken";
  public static final String AUTH_TOKEN_HEADER =  "X-AUTH-TOKEN";
  public static final String COOKIE_API_TOKEN = "apiToken";
  public static final String API_TOKEN_HEADER = "X-AUTH-YW-API-TOKEN";

  @Override
  public CompletionStage<Result> call(Http.Context ctx) {
    String token = fetchToken(ctx, true);
    Customer cust = null;
    if (token != null) {
      cust = Customer.authWithApiToken(token);
    } else {
      token = fetchToken(ctx, false);
      cust = Customer.authWithToken(token);
    }

    if (cust != null) {
      ctx.request().withUsername(cust.getEmail());
      ctx.args.put("customer", cust);
      return delegate.call(ctx);
    }
    // Send Forbidden Response if Authentication Fails
    return CompletableFuture.completedFuture(Results.forbidden("Unable To Authenticate Customer"));
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
