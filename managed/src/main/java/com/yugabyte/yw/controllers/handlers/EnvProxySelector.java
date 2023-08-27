/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers.handlers;

import com.google.common.annotations.VisibleForTesting;
import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.Proxy;
import java.net.Proxy.Type;
import java.net.ProxySelector;
import java.net.SocketAddress;
import java.net.URI;
import java.net.URISyntaxException;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.function.Function;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.jetbrains.annotations.NotNull;

@Slf4j
public class EnvProxySelector extends ProxySelector {

  private final Function<String, String> envVarGetter;

  final Optional<URI> httpProxy;
  final Optional<URI> httpsProxy;
  final Set<String> noProxies = new HashSet<>();

  private final ProxySelector prevSelector = ProxySelector.getDefault();

  public EnvProxySelector() {
    this(System::getenv);
  }

  @VisibleForTesting
  EnvProxySelector(Function<String, String> envVarGetter) {
    super();
    this.envVarGetter = envVarGetter;
    String[] httpProxyStrs =
        getEnvBothCase("http_proxy").map(s -> s.split(",")).orElse(new String[0]);

    httpProxy = initHttpProxy(httpProxyStrs);
    httpsProxy = initHttpsProxy(httpProxyStrs);

    initNoProxies();
  }

  private void initNoProxies() {
    String[] noProxyStrs = getEnvBothCase("no_proxy").map(s -> s.split(",")).orElse(new String[0]);

    Collections.addAll(noProxies, noProxyStrs);

    // detect loopback
    noProxies.add("localhost");
    noProxies.add("127.0.0.1");
    noProxies.add("[::1]");
    noProxies.add("0.0.0.0");
    noProxies.add("[::0]");
    httpProxy.ifPresent(p -> noProxies.add(p.getHost()));
    httpsProxy.ifPresent(p -> noProxies.add(p.getHost()));
  }

  private Optional<URI> initHttpsProxy(String[] httpProxyStrs) {
    Optional<URI> httpsProxy = getEnvBothCase("https_proxy").map(EnvProxySelector::toURI);
    for (String httpProxyStr : httpProxyStrs) {
      URI uri = toURI(httpProxyStr.trim());
      if ("https".equalsIgnoreCase(uri.getScheme())) {
        if (!httpsProxy.isPresent()) {
          httpsProxy = Optional.of(uri);
        }
      }
    }
    return httpsProxy;
  }

  private static Optional<URI> initHttpProxy(String[] httpProxyStrs) {
    for (String httpProxyStr : httpProxyStrs) {
      URI uri = toURI(httpProxyStr.trim());
      if ("http".equalsIgnoreCase(uri.getScheme())) {
        return Optional.of(uri);
      }
    }
    return Optional.empty();
  }

  private static URI toURI(String s) {
    try {
      return new URI(s);
    } catch (URISyntaxException e) {
      throw new IllegalArgumentException("Error parsing proxy env var. Invalid URI: " + s, e);
    }
  }

  private Optional<String> getEnvBothCase(String var) {
    String ret = envVarGetter.apply(var);
    if (StringUtils.isBlank(ret)) {
      ret = envVarGetter.apply(var.toUpperCase());
      if (StringUtils.isBlank(ret)) {
        ret = null;
      }
    }
    return Optional.ofNullable(ret);
  }

  private static Proxy toProxy(URI proxyUri) {
    return new Proxy(Type.HTTP, new InetSocketAddress(proxyUri.getHost(), proxyUri.getPort()));
  }

  @Override
  public List<Proxy> select(URI uri) {
    validateUri(uri);
    switch (uri.getScheme()) {
      case "http":
        return selectInternal(uri, httpProxy);
      case "https":
        return selectInternal(uri, httpsProxy);
      default:
        return prevSelector.select(uri);
    }
  }

  private static void validateUri(URI uri) {
    if (uri == null) {
      throw new IllegalArgumentException("URI can't be null.");
    }
    if (uri.getScheme() == null || uri.getHost() == null) {
      throw new IllegalArgumentException(
          "protocol = " + uri.getScheme() + " host = " + uri.getHost());
    }
  }

  @NotNull
  private List<Proxy> selectInternal(URI uri, Optional<URI> proxyUri) {
    return Collections.singletonList(
        proxyUri
            .filter(ignore -> shouldUseProxy(uri))
            .map(EnvProxySelector::toProxy)
            .orElse(Proxy.NO_PROXY));
  }

  private boolean shouldUseProxy(URI uri) {
    String host = uri.getHost();
    // int port = uri.getPort(); TODO(sbapat) port match is a rare use case. punt/K.I.S.S.
    // Do suffix match on host part
    return noProxies.stream().noneMatch(host::endsWith);
  }

  @Override
  public void connectFailed(URI uri, SocketAddress socketAddress, IOException e) {
    log.warn("Failed to connect to " + uri, e);
  }
}
