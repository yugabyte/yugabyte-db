// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.pa;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.containsString;
import static org.hamcrest.Matchers.equalTo;

import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.Customer;
import java.io.IOException;
import java.util.Map;
import java.util.concurrent.TimeUnit;
import okhttp3.mockwebserver.MockResponse;
import okhttp3.mockwebserver.MockWebServer;
import okhttp3.mockwebserver.RecordedRequest;
import org.junit.AfterClass;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.Timeout;
import play.Application;

/**
 * Covers how often {@link PACollectorSync} pushes customer_metadata to the embedded collector,
 * which is what decides whether a collector that stopped answering raises
 * PA_EMBEDDED_COLLECTOR_ERROR ahead of PA_COLLECTOR_DOWN.
 *
 * <p>Separate from {@link PACollectorSyncTest} because the embedded path needs yb.pa.url in the
 * static config, so the collector's address has to be known before the application is built.
 */
public class PACollectorSyncEmbeddedTest extends FakeDBApplication {

  private static MockWebServer embeddedPa;

  @Rule public Timeout perTestTimeout = Timeout.builder().withTimeout(2, TimeUnit.MINUTES).build();

  private Customer customer;
  private PACollectorSync sync;

  @Override
  protected Application provideApplication() {
    // Started here, not in setUp: the URL has to go into the config the application is built with.
    embeddedPa = new MockWebServer();
    try {
      embeddedPa.start();
    } catch (IOException e) {
      throw new RuntimeException("Could not start the embedded PA stand-in", e);
    }
    Map<String, Object> config =
        ImmutableMap.of(
            "yb.pa.url",
            "http://" + embeddedPa.getHostName() + ":" + embeddedPa.getPort(),
            "yb.pa.api_token",
            "test-pa-token");
    return provideApplication(config);
  }

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    sync = app.injector().instanceOf(PACollectorSync.class);
    // Global runtime config outlives a test run in the shared test database, so the value this
    // test flips has to start from a known state rather than its declared default.
    mutableConfigFactory
        .globalRuntimeConf()
        .setValue(GlobalConfKeys.paEmbeddedUiReverseProxyEnabled.getKey(), "false");
  }

  @AfterClass
  public static void tearDownClass() throws IOException {
    embeddedPa.shutdown();
  }

  @Test
  public void testTheCollectorIsCreatedOnceAndThenLeftAlone() throws Exception {
    // One response only: the create. If a later tick pushes anything, it finds nothing enqueued
    // and blocks until the client gives up, so a regression here shows as a slow failure.
    embeddedPa.enqueue(new MockResponse().setBody("{}"));
    sync.initialize(customer);

    RecordedRequest create = embeddedPa.takeRequest();
    assertThat(create.getMethod(), equalTo("PUT"));
    assertThat(
        create.getPath(), containsString("/api/customer/" + customer.getUuid() + "/metadata"));

    int afterCreate = embeddedPa.getRequestCount();
    sync.initialize(customer);
    sync.initialize(customer);
    assertThat(embeddedPa.getRequestCount() - afterCreate, equalTo(0));
  }

  @Test
  public void testAChangedValueIsPushedAgain() throws Exception {
    embeddedPa.enqueue(new MockResponse().setBody("{}"));
    sync.initialize(customer);
    embeddedPa.takeRequest();
    int afterCreate = embeddedPa.getRequestCount();

    // proxyMode is in the body and comes from runtime config, so flipping it has to be pushed.
    mutableConfigFactory
        .globalRuntimeConf()
        .setValue(GlobalConfKeys.paEmbeddedUiReverseProxyEnabled.getKey(), "true");

    // Guards the lever itself: if the flip is not visible to the code that builds the body, the
    // rest of this test would pass for the wrong reason.
    assertThat(
        app.injector()
            .instanceOf(RuntimeConfGetter.class)
            .getGlobalConf(GlobalConfKeys.paEmbeddedUiReverseProxyEnabled),
        equalTo(true));

    embeddedPa.enqueue(new MockResponse().setBody("{}"));
    sync.initialize(customer);
    assertThat(embeddedPa.getRequestCount() - afterCreate, equalTo(1));

    RecordedRequest update = embeddedPa.takeRequest();
    assertThat(update.getMethod(), equalTo("PUT"));
    assertThat(update.getBody().readUtf8(), containsString("\"proxyMode\" : true"));
  }

  @Test
  public void testAFailedPushIsRetriedOnTheNextTick() throws Exception {
    // Nothing is recorded until the collector accepts the push, so the tick after a failure has
    // to send it again.
    embeddedPa.enqueue(new MockResponse().setResponseCode(503));
    sync.initialize(customer);
    embeddedPa.takeRequest();

    embeddedPa.enqueue(new MockResponse().setBody("{}"));
    sync.initialize(customer);
    RecordedRequest retry = embeddedPa.takeRequest();
    assertThat(retry.getMethod(), equalTo("PUT"));

    // And once accepted, it stops.
    int afterRetry = embeddedPa.getRequestCount();
    sync.initialize(customer);
    assertThat(embeddedPa.getRequestCount() - afterRetry, equalTo(0));
  }
}
