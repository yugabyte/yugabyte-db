// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.common.controllers;

import play.mvc.Action;
import play.mvc.Http;
import play.mvc.Result;
import play.mvc.Results;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;

import com.yugabyte.yw.api.models.Customer;

public class TokenAuthenticator extends Action.Simple {
	public static final String COOKIE_AUTH_TOKEN = "authToken";
	public static final String API_AUTH_TOKEN =  "X-AUTH-TOKEN";

  @Override
  public CompletionStage<Result> call(Http.Context ctx) {
    String token = getTokenFromHeader(ctx);

    if (token != null) {
      Customer cust = Customer.authWithToken(token);
      if (cust != null) {
        ctx.request().withUsername(cust.getEmail());
        ctx.args.put("customer_uuid", cust.uuid);
        return delegate.call(ctx);
      }
    }
		// TODO: we need to handle the API route with a JSON response.
    return CompletableFuture.completedFuture(Results.redirect("/login"));
  }

  private String getTokenFromHeader(Http.Context ctx) {
    String[] authTokenHeader = ctx.request().headers().get(API_AUTH_TOKEN);
	  Http.Cookie cookieAuthToken = ctx.request().cookie(COOKIE_AUTH_TOKEN);

    if ((authTokenHeader != null) && (authTokenHeader.length == 1)) {
        return authTokenHeader[0];
    } else if (cookieAuthToken != null) {
			// If we are accessing authenticated pages, the auth token would be in the cookie
			return cookieAuthToken.value();
    }
	  return null;
  }
}
