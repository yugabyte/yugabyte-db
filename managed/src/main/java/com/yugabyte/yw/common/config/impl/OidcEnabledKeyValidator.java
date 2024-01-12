package com.yugabyte.yw.common.config.impl;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfigPreChangeValidator;
import java.util.UUID;

@Singleton
public class OidcEnabledKeyValidator implements RuntimeConfigPreChangeValidator {

  @Inject private Config staticConfig;

  @Override
  public String getKeyPath() {
    return GlobalConfKeys.useOauth.getKey();
  }

  @Override
  public void validateConfigGlobal(UUID scopeUUID, String path, String newValue) {
    boolean multiTenant = staticConfig.getBoolean("yb.multiTenant");
    if (multiTenant && newValue.equals("true")) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot enable OIDC on multi tenant environment");
    }
  }
}
