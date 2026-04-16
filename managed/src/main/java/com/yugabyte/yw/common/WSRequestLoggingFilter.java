/*
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import javax.inject.Singleton;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;
import play.libs.ws.StandaloneWSRequest;
import play.libs.ws.WSRequestExecutor;
import play.libs.ws.WSRequestFilter;
import play.libs.ws.ahc.StandaloneAhcWSRequest;
import play.shaded.ahc.org.asynchttpclient.Request;
import play.shaded.ahc.org.asynchttpclient.proxy.ProxyServer;

/**
 * A {@link WSRequestFilter} that logs request details: HTTP method, URL, and proxy host (when a
 * proxy is used). Uses the same AHC request building approach as {@link
 * play.libs.ws.ahc.AhcCurlRequestLogger}.
 */
@Slf4j
@Singleton
public class WSRequestLoggingFilter implements WSRequestFilter {

  @Setter private boolean logRequests = false;

  @Inject
  public WSRequestLoggingFilter(RuntimeConfGetter confGetter) {
    this.logRequests = Boolean.TRUE.equals(confGetter.getGlobalConf(GlobalConfKeys.logWSRequests));
  }

  @Override
  public WSRequestExecutor apply(WSRequestExecutor requestExecutor) {
    return request -> {
      logRequestDetails(request);
      return requestExecutor.apply(request);
    };
  }

  private void logRequestDetails(StandaloneWSRequest request) {
    if (!logRequests) {
      return;
    }
    String method = request.getMethod();
    String url = request.getUrl();
    String proxyHost = null;

    if (request instanceof StandaloneAhcWSRequest) {
      Request ahcRequest = buildRequest((StandaloneAhcWSRequest) request);
      if (ahcRequest != null) {
        ProxyServer proxyServer = ahcRequest.getProxyServer();
        if (proxyServer != null) {
          proxyHost = proxyServer.getHost() + ":" + proxyServer.getPort();
        }
      }
    }

    if (proxyHost != null) {
      log.debug("WS request: method={}, url={}, proxyHost={}", method, url, proxyHost);
    } else {
      log.debug("WS request: method={}, url={}, proxy is not used", method, url);
    }
  }

  /**
   * Calls package-private {@code buildRequest()} on StandaloneAhcWSRequest via reflection.
   *
   * @return the AHC Request, or null if reflection fails
   */
  private static Request buildRequest(StandaloneAhcWSRequest request) {
    try {
      Method buildRequest = StandaloneAhcWSRequest.class.getDeclaredMethod("buildRequest");
      buildRequest.setAccessible(true);
      return (Request) buildRequest.invoke(request);
    } catch (NoSuchMethodException | IllegalAccessException | InvocationTargetException e) {
      log.trace("Could not get AHC request for proxy logging", e);
      return null;
    }
  }
}
