package com.yugabyte.ByocApiProxy.auth;

import com.yugabyte.aeon.client.ApiClient;
import com.yugabyte.aeon.client.auth.HttpBearerAuth;

public class ApiKeyAuthenticator implements BaseAuthenticator {
  private final String apiKey;

  public ApiKeyAuthenticator(String apiKey) {
    this.apiKey = apiKey;
  }

  @Override
  public void authenticate(ApiClient client) {
    HttpBearerAuth bearer = (HttpBearerAuth) client.getAuthentication("BearerAuthToken");
    bearer.setBearerToken(apiKey);
  }
}
