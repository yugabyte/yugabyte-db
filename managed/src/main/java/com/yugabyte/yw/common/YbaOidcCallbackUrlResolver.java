// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import java.util.Locale;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.pac4j.core.context.WebContext;
import org.pac4j.core.http.callback.CallbackUrlResolver;
import org.pac4j.core.http.callback.PathParameterCallbackUrlResolver;
import org.pac4j.core.http.callback.QueryParameterCallbackUrlResolver;
import org.pac4j.core.http.url.UrlResolver;

@Slf4j
@Singleton
public class YbaOidcCallbackUrlResolver implements CallbackUrlResolver {

  public enum OidcCallbackMode {
    QUERY("query"),
    PATH("path");

    private final String configValue;

    OidcCallbackMode(String configValue) {
      this.configValue = configValue;
    }

    public String getConfigValue() {
      return configValue;
    }

    public static OidcCallbackMode fromConfigValue(String value) {
      if (value != null) {
        String normalizedValue = value.toLowerCase(Locale.ROOT);
        for (OidcCallbackMode mode : values()) {
          if (mode.configValue.equals(normalizedValue)) {
            return mode;
          }
        }
      }
      throw new IllegalArgumentException(
          String.format("Supported values are: %s, %s", QUERY.configValue, PATH.configValue));
    }
  }

  private final RuntimeConfGetter confGetter;

  private final QueryParameterCallbackUrlResolver queryResolver =
      new QueryParameterCallbackUrlResolver();
  private final PathParameterCallbackUrlResolver pathResolver =
      new PathParameterCallbackUrlResolver();

  @Inject
  public YbaOidcCallbackUrlResolver(RuntimeConfGetter confGetter) {
    this.confGetter = confGetter;
  }

  @Override
  public String compute(
      UrlResolver urlResolver, String url, String clientName, WebContext context) {
    CallbackUrlResolver delegate = resolveDelegate();
    return delegate.compute(urlResolver, url, clientName, context);
  }

  @Override
  public boolean matches(String clientName, WebContext context) {
    return resolveDelegate().matches(clientName, context);
  }

  private CallbackUrlResolver resolveDelegate() {
    OidcCallbackMode mode = confGetter.getGlobalConf(GlobalConfKeys.oidcCallbackMode);
    log.debug("Resolving OIDC callback delegate for mode: {}", mode);

    return switch (mode) {
      case PATH -> pathResolver;
      default -> queryResolver;
    };
  }
}
