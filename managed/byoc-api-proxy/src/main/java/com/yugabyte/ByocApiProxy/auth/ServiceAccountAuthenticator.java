package com.yugabyte.ByocApiProxy.auth;

import com.yugabyte.ByocApiProxy.config.ProxiedAppProperties;
import com.yugabyte.aeon.client.ApiClient;
import com.yugabyte.aeon.client.ApiException;
import com.yugabyte.aeon.client.api.InternalAuthApi;
import com.yugabyte.aeon.client.api.UiApi;
import com.yugabyte.aeon.client.auth.HttpBearerAuth;
import com.yugabyte.aeon.client.models.AdminApiTokenResponse;
import com.yugabyte.aeon.client.models.LoginRequest;
import com.yugabyte.aeon.client.models.LoginResponse;
import java.time.Duration;
import java.time.OffsetDateTime;

public class ServiceAccountAuthenticator implements BaseAuthenticator {
  private final String email;
  private final String password;
  private final Duration refreshInterval;

  private OffsetDateTime lastRefresh;

  public ServiceAccountAuthenticator(ProxiedAppProperties.ServiceAccount account) {
    super();
    this.email = account.email();
    this.password = account.password();
    this.refreshInterval = account.refreshInterval();
  }

  @Override
  public void authenticate(ApiClient client) throws ApiException {
    if (lastRefresh == null || lastRefresh.isBefore(OffsetDateTime.now().minus(refreshInterval))) {
      UiApi uiApi = new UiApi(client);
      InternalAuthApi internalAuthApi = new InternalAuthApi(client);
      HttpBearerAuth bearer = (HttpBearerAuth) client.getAuthentication("BearerAuthToken");
      LoginResponse login = uiApi.loginUser(new LoginRequest().email(email).password(password));
      bearer.setBearerToken(login.getData().getAuthToken());
      AdminApiTokenResponse admin =
          internalAuthApi.getAdminApiToken(
              null /* userId */, null /* impersonatingUserEmail */, null /* accountId */);
      String adminJwt = admin.getData().getAdminJwt();
      lastRefresh = OffsetDateTime.now();
      bearer.setBearerToken(adminJwt);
    }
  }
}
