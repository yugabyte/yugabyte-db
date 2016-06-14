// Copyright (c) Yugabyte, Inc.

package security;

import com.fasterxml.jackson.databind.node.ObjectNode;
import models.yb.Customer;
import play.libs.Json;
import play.mvc.Action;
import play.mvc.Http;
import play.mvc.Result;
import play.mvc.Results;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;

public class TokenAuthenticator extends Action.Simple {

  @Override
  public CompletionStage<Result> call(Http.Context ctx) {
    String token = getTokenFromHeader(ctx);

    if (token != null) {
        Customer cust = Customer.authWithToken(token);
        if (cust != null) {
            ctx.request().withUsername(cust.getEmail());
            ctx.args.put("customer", cust);
            return delegate.call(ctx);
        }
    }
	  ObjectNode responseJson = Json.newObject();
	  responseJson.put("error", "Invalid AuthToken");
    return CompletableFuture.completedFuture(Results.unauthorized(responseJson));
  }

  private String getTokenFromHeader(Http.Context ctx) {
    String[] authTokenHeader = ctx.request().headers().get("X-AUTH-TOKEN");

    if ((authTokenHeader != null) && (authTokenHeader.length == 1)) {
        return authTokenHeader[0];
    }
    return null;
  }
}
