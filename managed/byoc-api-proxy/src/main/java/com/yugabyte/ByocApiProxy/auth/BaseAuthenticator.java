package com.yugabyte.ByocApiProxy.auth;

import com.yugabyte.aeon.client.ApiClient;
import com.yugabyte.aeon.client.ApiException;

public interface BaseAuthenticator {
  void authenticate(ApiClient client) throws ApiException;
}
