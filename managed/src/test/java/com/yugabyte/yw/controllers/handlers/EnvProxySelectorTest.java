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

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;

import com.google.common.base.Functions;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.controllers.UniverseControllerTestBase;
import java.net.Proxy;
import java.net.ProxySelector;
import java.net.URI;
import java.net.URISyntaxException;
import org.junit.Assert;
import org.junit.Test;

public class EnvProxySelectorTest extends UniverseControllerTestBase {

  private static final String MY_HTTP_PROXY = "http://sbapat.proxy.yugabyte.com:1234";
  private static final String MY_HTTPS_PROXY = "https://sbapat.proxy.yugabyte.com:1234";

  @Test
  public void testNoProxySetup() throws URISyntaxException {
    EnvProxySelector envProxySelector =
        new EnvProxySelector(Functions.forMap(ImmutableMap.of(), null));
    assertFalse(envProxySelector.httpProxy.isPresent());
    assertFalse(envProxySelector.httpsProxy.isPresent());
    assertNoProxy(envProxySelector, "http://oidc.provider.com:345");
    assertNoProxy(envProxySelector, "https://oidc.provider.com:345");
  }

  private void assertNoProxy(EnvProxySelector envProxySelector, String urlStr)
      throws URISyntaxException {
    assertEquals(ImmutableList.of(Proxy.NO_PROXY), envProxySelector.select(new URI(urlStr)));
  }

  @Test
  public void testHttp() throws URISyntaxException {
    testHttp("http_proxy");
  }

  @Test
  public void testHttpUpperCase() throws URISyntaxException {
    testHttp("HTTP_PROXY");
  }

  void testHttp(String http_proxy) throws URISyntaxException {
    EnvProxySelector envProxySelector =
        new EnvProxySelector(Functions.forMap(ImmutableMap.of(http_proxy, MY_HTTP_PROXY), null));
    assertEquals(new URI(MY_HTTP_PROXY), envProxySelector.httpProxy.get());
    assertFalse(envProxySelector.httpsProxy.isPresent());

    assertNoProxy(envProxySelector, MY_HTTP_PROXY);
    assertNoProxy(envProxySelector, "http://localhost:98");
    assertNoProxy(envProxySelector, "http://127.0.0.1:23");
    assertNoProxy(envProxySelector, "http://0.0.0.0:23");
    assertNoProxy(envProxySelector, "http://[::0]");
    assertNoProxy(envProxySelector, "http://[::1]");

    assertEquals(
        ImmutableList.of(Proxy.NO_PROXY),
        envProxySelector.select(new URI("https://oidc.provider.com:345")));

    assertNotEquals(
        ImmutableList.of(Proxy.NO_PROXY),
        envProxySelector.select(new URI("http://oidc.provider.com:345")));
  }

  @Test
  public void testHttps() throws URISyntaxException {
    testHttps("https_proxy");
  }

  @Test
  public void testHttpsUpperCase() throws URISyntaxException {
    testHttps("HTTPS_PROXY");
  }

  void testHttps(String https_proxy) throws URISyntaxException {
    EnvProxySelector envProxySelector =
        new EnvProxySelector(Functions.forMap(ImmutableMap.of(https_proxy, MY_HTTPS_PROXY), null));
    assertEquals(new URI(MY_HTTPS_PROXY), envProxySelector.httpsProxy.get());
    assertFalse(envProxySelector.httpProxy.isPresent());

    assertNoProxy(envProxySelector, MY_HTTP_PROXY);
    assertNoProxy(envProxySelector, "https://localhost:98");
    assertNoProxy(envProxySelector, "https://127.0.0.1:23");
    assertNoProxy(envProxySelector, "https://0.0.0.0:23");
    assertNoProxy(envProxySelector, "https://[::0]");
    assertNoProxy(envProxySelector, "https://[::1]");

    assertEquals(
        ImmutableList.of(Proxy.NO_PROXY),
        envProxySelector.select(new URI("http://oidc.provider.com:345")));

    assertNotEquals(
        ImmutableList.of(Proxy.NO_PROXY),
        envProxySelector.select(new URI("https://oidc.provider.com:345")));
  }

  @Test
  public void testIllegalArgForNull() {
    assertFailsWithIAE(new EnvProxySelector(), null);
  }

  @Test
  public void testIllegalArgForNoHost() throws Exception {
    final ProxySelector selector = new EnvProxySelector();
    assertFailsWithIAE(selector, new URI("http", "/test", null));
    assertFailsWithIAE(selector, new URI("https", "/test2", null));
    assertFailsWithIAE(selector, new URI("ftp", "/test3", null));
  }

  @Test
  public void testIllegalArgForNoScheme() throws Exception {
    assertFailsWithIAE(new EnvProxySelector(), new URI(null, "/test", null));
  }

  private static void assertFailsWithIAE(final ProxySelector selector, final URI uri) {
    try {
      selector.select(uri);
      Assert.fail("select() was expected to fail for URI " + uri);
    } catch (IllegalArgumentException iae) {
      // expected
    }
  }
}
