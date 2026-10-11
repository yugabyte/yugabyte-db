// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.mockito.Mockito.mock;
import static play.inject.Bindings.bind;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.cloud.aws.AWSCloudImpl;
import com.yugabyte.yw.commissioner.HealthChecker;
import com.yugabyte.yw.common.alerts.AlertConfigurationWriter;
import com.yugabyte.yw.common.alerts.AlertsGarbageCollector;
import com.yugabyte.yw.common.alerts.QueryAlerts;
import com.yugabyte.yw.common.config.RuntimeConfigCache;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import java.lang.reflect.Field;
import java.lang.reflect.Modifier;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeoutException;
import org.apache.pekko.stream.Materializer;
import org.apache.pekko.stream.javadsl.Source;
import org.apache.pekko.util.ByteString;
import org.junit.After;
import org.junit.AfterClass;
import org.junit.Before;
import org.junit.BeforeClass;
import org.mockito.Mockito;
import org.pac4j.play.LogoutController;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.mvc.Http;
import play.mvc.Http.MultipartFormData.Part;
import play.mvc.Http.Request;
import play.mvc.Http.RequestBuilder;
import play.mvc.Result;
import play.test.Helpers;
import play.test.TestServer;

/**
 * Base class for all Play/Guice backed tests.
 *
 * <p>Historically this extended Play's {@link play.test.WithServer}, which built a brand new Guice
 * application (instantiating the entire {@code MainModule} eager-singleton graph plus a fresh Ebean
 * server) and started a real embedded Netty HTTP server for <b>every</b> test method. That cost
 * ~6-7 seconds per method regardless of what the test actually did.
 *
 * <p>To make tests dramatically faster we now:
 *
 * <ul>
 *   <li>Manage the application lifecycle ourselves (no Netty server: no test uses the running
 *       server, everything goes through {@link Helpers#route}).
 *   <li>Build the application <b>once per test class</b> and reuse it across all of that class'
 *       methods when the class does not customize the wiring (see {@link
 *       #isReusableApplication()}). Per-method isolation is restored by truncating the database and
 *       resetting mocks before each method rather than by rebuilding the whole application.
 *   <li>Fall back to the previous per-method build for classes that override {@code
 *       provideApplication()} (they may bind different mocks/config per method).
 * </ul>
 *
 * <p>Because a reused application captures the mock instances that existed at build time (Guice
 * eager singletons hold references to them), we reflectively snapshot every mock-valued field of
 * the first test instance, restore those same instances into every subsequent test instance, and
 * {@link Mockito#reset} them before each method. That keeps the instances the application sees and
 * the instances the test stubs in perfect sync while still giving each method a clean mock state.
 */
public abstract class PlatformGuiceApplicationBaseTest {

  /** The running application (shared across methods when {@link #isReusableApplication()}). */
  protected Application app;

  /** The application's Pekko streams Materializer. */
  protected Materializer mat;

  /** The TCP port the embedded HTTP server is running on (for tests that make real HTTP calls). */
  protected int port = -1;

  /** The embedded HTTP server used for the current per-method (non-reusable) test. */
  protected TestServer testServer;

  protected HealthChecker mockHealthChecker;
  protected QueryAlerts mockQueryAlerts;
  protected AlertsGarbageCollector mockAlertsGarbageCollector;
  protected AlertConfigurationWriter mockAlertConfigurationWriter;
  protected AWSCloudImpl mockAWSCloudImpl;
  protected LogoutController mockLogoutController;

  protected Request fakeRequest = Helpers.fakeRequest().build();

  // ---------------------------------------------------------------------------
  // Per-class reusable application state (per forked test JVM).
  // ---------------------------------------------------------------------------
  private static Application reusableApp;
  private static TestServer reusableTestServer;
  private static int reusablePort = -1;
  private static String reusableAppClass;
  // Snapshot of the mock-valued fields of the first instance that built the reusable app, keyed by
  // the reflective Field so we can push the exact same instances into later test instances.
  private static Map<Field, Object> reusableMockFields;

  // Whether the current test method is running against the shared reusable app (vs a freshly built
  // per-method app). Controls teardown behaviour.
  private boolean usingReusableApp;

  protected GuiceApplicationBuilder configureApplication(GuiceApplicationBuilder builder) {
    mockHealthChecker = mock(HealthChecker.class);
    mockQueryAlerts = mock(QueryAlerts.class);
    mockAlertConfigurationWriter = mock(AlertConfigurationWriter.class);
    mockAlertsGarbageCollector = mock(AlertsGarbageCollector.class);
    mockAWSCloudImpl = mock(AWSCloudImpl.class);
    mockLogoutController = mock(LogoutController.class);

    return builder
        .overrides(bind(HealthChecker.class).toInstance(mockHealthChecker))
        .overrides(bind(QueryAlerts.class).toInstance(mockQueryAlerts))
        .overrides(bind(AlertConfigurationWriter.class).toInstance(mockAlertConfigurationWriter))
        .overrides(bind(AWSCloudImpl.class).toInstance(mockAWSCloudImpl))
        .overrides(bind(LogoutController.class).toInstance(mockLogoutController))
        .overrides(bind(AlertsGarbageCollector.class).toInstance(mockAlertsGarbageCollector));
  }

  /**
   * Override this method to set up the application to use. Subclasses that override it opt out of
   * per-class application reuse (they are rebuilt for every method), because they may bind
   * different mocks/config per method.
   *
   * @return the application to use.
   */
  protected Application provideApplication() {
    return Helpers.fakeApplication();
  }

  /**
   * Override this method to set up the port to use.
   *
   * @return the TCP port used by the embedded server.
   */
  protected int providePort() {
    return play.api.test.Helpers.testServerPort();
  }

  protected <T> T instanceOf(Class<T> clazz) {
    return app.injector().instanceOf(clazz);
  }

  /**
   * Whether the concrete test class can share a single application instance across all of its test
   * methods.
   *
   * <p>By default a class is reusable iff it does not customize the wiring, i.e. the most-derived
   * declarer of {@code provideApplication()} is {@link FakeDBApplication} (see {@link
   * #usesDefaultProvideApplication()}). Per-method isolation is then achieved by truncating the
   * database and resetting mocks before each method rather than by rebuilding the application.
   *
   * <p>Base classes that <b>do</b> override {@code provideApplication()} but wire the application
   * <b>identically for every method</b> (same set of {@code bind(...).toInstance(mockX)} overrides,
   * with the mocks held in plain instance fields) can opt back in by overriding this method to
   * return {@code true}. The reuse machinery reflectively snapshots those mock fields from the
   * first instance, restores the same instances into later instances, and resets them per method,
   * so the objects the application holds and the objects the test stubs stay in sync. A subclass
   * that needs genuinely per-method wiring (e.g. it re-overrides {@code provideApplication()} to
   * bind different things per method) must override this to return {@code false}.
   */
  protected boolean reusableApplication() {
    return usesDefaultProvideApplication();
  }

  /**
   * True iff the concrete test class (and every class between it and {@link FakeDBApplication})
   * uses FakeDBApplication's default {@code provideApplication()} unchanged.
   */
  protected final boolean usesDefaultProvideApplication() {
    for (Class<?> c = getClass(); c != null; c = c.getSuperclass()) {
      try {
        c.getDeclaredMethod("provideApplication");
        return c == FakeDBApplication.class;
      } catch (NoSuchMethodException ignore) {
        // keep walking up
      }
    }
    return false;
  }

  private boolean isReusableApplication() {
    if (!reusableApplication()) {
      return false;
    }
    // Classes that use FakeDBApplication's default wiring keep reuse unconditionally (this is the
    // long-standing behavior): their mocks are not bound into the application, so their stubbings
    // are only exercised inside the test body and a strict Mockito runner is happy either way.
    //
    // Classes that opt into reuse while overriding provideApplication() bind their own mocks into
    // the application and stub them in setUp. Some of those stubs are consumed by application
    // startup (AppInit constructing services that call the mocks). With a per-method rebuild that
    // counts as "used" every method; with a reused application startup runs only once, so in later
    // methods a strict Mockito runner flags them as UnnecessaryStubbing and fails the class. Rather
    // than chase such startup-defensive stubs class by class, we do not reuse the application for
    // those strict-Mockito classes. Parameterized/plain-JUnit classes still reuse it.
    if (!usesDefaultProvideApplication()
        && usesStrictMockitoRunner()
        && !reuseAppDespiteStrictMockito()) {
      return false;
    }
    return true;
  }

  /**
   * Escape hatch for a strict-Mockito test class that overrides {@code provideApplication()} but
   * has been verified to reuse the application safely, i.e. none of its setUp stubbings are
   * consumed only by application startup (or all such stubbings are marked {@code lenient()}). Such
   * a class overrides this to return {@code true} to opt back into per-class application reuse. See
   * {@link #isReusableApplication()} for why strict-Mockito classes are otherwise not reused.
   */
  protected boolean reuseAppDespiteStrictMockito() {
    return false;
  }

  private boolean usesStrictMockitoRunner() {
    for (Class<?> c = getClass(); c != null && c != Object.class; c = c.getSuperclass()) {
      org.junit.runner.RunWith runWith = c.getAnnotation(org.junit.runner.RunWith.class);
      if (runWith != null) {
        return org.mockito.junit.MockitoJUnitRunner.class.isAssignableFrom(runWith.value())
            // MockitoJUnitRunner.Silent is lenient and would be safe, but it is rarely used and not
            // worth special casing; treat any Mockito runner as non-reusable.
            && !runWith.value().getSimpleName().equals("Silent");
      }
    }
    return false;
  }

  @BeforeClass
  public static void clearMocks() {
    Mockito.framework().clearInlineMocks();
  }

  @Before
  public void platformBaseSetUp() {
    // Reset to a freshly-migrated (empty) state BEFORE building/using the application. The very
    // first application build in the JVM runs flyway (migration.auto=true); every later build
    // disables it. resetDatabase() is a no-op until that first migration has run.
    TestPostgres.ensureStarted();
    TestPostgres.resetDatabase();

    usingReusableApp = isReusableApplication();
    String className = getClass().getName();

    if (usingReusableApp) {
      if (reusableApp == null || !className.equals(reusableAppClass)) {
        // First method of this reusable class (or a stale app from a previous class): build once.
        stopReusableApplication();
        Application built = provideApplication();
        TestServer server = Helpers.testServer(providePort(), built);
        server.start();
        reusableApp = built;
        reusableTestServer = server;
        reusablePort = server.getRunningHttpPort().getAsInt();
        reusableAppClass = className;
        app = built;
        reusableMockFields = snapshotMockFields();
      } else {
        // Reuse the app built for the first method: push the captured mock instances into this
        // (fresh) test instance so the test stubs the very objects the application holds.
        app = reusableApp;
        restoreMockFields(reusableMockFields);
      }
      port = reusablePort;
      resetMocks(reusableMockFields);
      clearPerMethodInMemoryState();
    } else {
      app = provideApplication();
      testServer = Helpers.testServer(providePort(), app);
      testServer.start();
      port = testServer.getRunningHttpPort().getAsInt();
    }

    mat = app.asScala().materializer();
    // The application is started/stopped per class now, but static shutdown state is not reset as
    // the JVM does not restart between methods.
    Util.resetYbaShutdownStarted();
  }

  @After
  public void baseTearDown() {
    if (!usingReusableApp && testServer != null) {
      try {
        testServer.stop();
      } finally {
        TestHelper.shutdownDatabase();
        testServer = null;
        app = null;
        port = -1;
      }
    }
  }

  @AfterClass
  public static void stopReusableApplication() {
    if (reusableTestServer != null) {
      try {
        reusableTestServer.stop();
      } finally {
        TestHelper.shutdownDatabase();
        reusableApp = null;
        reusableTestServer = null;
        reusablePort = -1;
        reusableAppClass = null;
        reusableMockFields = null;
      }
    }
  }

  // ---------------------------------------------------------------------------
  // Reflection helpers for reusing mock instances across method-scoped test instances.
  // ---------------------------------------------------------------------------

  private Map<Field, Object> snapshotMockFields() {
    Map<Field, Object> fields = new LinkedHashMap<>();
    for (Class<?> c = getClass(); c != null && c != Object.class; c = c.getSuperclass()) {
      for (Field f : c.getDeclaredFields()) {
        if (Modifier.isStatic(f.getModifiers()) || isMockitoManaged(f)) {
          // @Mock/@Spy/@InjectMocks fields are (re)created and validated per test method by the
          // Mockito runner/rule; leave them alone so reuse does not fight strict-stub tracking.
          continue;
        }
        try {
          f.setAccessible(true);
          Object value = f.get(this);
          if (value != null && Mockito.mockingDetails(value).isMock()) {
            fields.put(f, value);
          }
        } catch (ReflectiveOperationException | RuntimeException ignore) {
          // Field not accessible / not readable - skip it.
        }
      }
    }
    return fields;
  }

  private static boolean isMockitoManaged(Field f) {
    for (java.lang.annotation.Annotation a : f.getAnnotations()) {
      String name = a.annotationType().getName();
      if (name.equals("org.mockito.Mock")
          || name.equals("org.mockito.Spy")
          || name.equals("org.mockito.InjectMocks")
          || name.equals("org.mockito.Captor")) {
        return true;
      }
    }
    return false;
  }

  private void restoreMockFields(Map<Field, Object> fields) {
    if (fields == null) {
      return;
    }
    for (Map.Entry<Field, Object> e : fields.entrySet()) {
      try {
        e.getKey().set(this, e.getValue());
      } catch (ReflectiveOperationException | RuntimeException ignore) {
        // Field belongs to a different instance shape - skip it.
      }
    }
  }

  private void resetMocks(Map<Field, Object> fields) {
    if (fields == null || fields.isEmpty()) {
      return;
    }
    Mockito.reset(fields.values().toArray());
  }

  // Reset in-memory singletons that hold DB-independent state so a reused application looks like a
  // freshly built one for every method (the database itself is truncated separately per method).
  private void clearPerMethodInMemoryState() {
    try {
      app.injector().instanceOf(SettableRuntimeConfigFactory.class).clearCache();
    } catch (RuntimeException ignore) {
      // Runtime config factory not bound for this app - nothing to clear.
    }
    try {
      app.injector().instanceOf(RuntimeConfigCache.class).invalidateCache();
    } catch (RuntimeException ignore) {
      // Runtime config cache not bound for this app - nothing to clear.
    }
    try {
      app.injector().instanceOf(com.yugabyte.yw.common.metrics.MetricStorage.class).clear();
    } catch (RuntimeException ignore) {
      // Metric storage not bound for this app - nothing to clear.
    }
  }

  public Result doRequest(String method, String url) {
    return FakeApiHelper.doRequest(app, method, url);
  }

  public Result doGetRequestNoAuth(String url) {
    return FakeApiHelper.doGetRequestNoAuth(app, url);
  }

  public Result doRequestWithAuthToken(String method, String url, String authToken) {
    return FakeApiHelper.doRequestWithAuthToken(app, method, url, authToken);
  }

  public Result doRequestWithJWT(String method, String url, String authToken) {
    return FakeApiHelper.doRequestWithJWT(app, method, url, authToken);
  }

  public Result doRequestWithCustomHeaders(String method, String url, Map<String, String> headers) {
    return FakeApiHelper.doRequestWithCustomHeaders(app, method, url, headers);
  }

  public Result doRequestWithHAToken(String method, String url, String haToken) {
    return FakeApiHelper.doRequestWithHAToken(app, method, url, haToken);
  }

  public Result doRequestWithHATokenAndBody(
      String method, String url, String haToken, JsonNode body) {
    return FakeApiHelper.doRequestWithHATokenAndBody(app, method, url, haToken, body);
  }

  public Result doRequestWithBody(String method, String url, JsonNode body) {
    return FakeApiHelper.doRequestWithBody(app, method, url, body);
  }

  public Result doRequestWithAuthTokenAndBody(
      String method, String url, String authToken, JsonNode body) {
    return FakeApiHelper.doRequestWithAuthTokenAndBody(app, method, url, authToken, body);
  }

  public Result doRequestWithBodyAndWithoutAuthToken(String method, String url, JsonNode body) {
    return FakeApiHelper.doRequestWithBodyAndWithoutAuthToken(app, method, url, body);
  }

  public Result doRequestWithJWTAndBody(
      String method, String url, String authToken, JsonNode body) {
    return FakeApiHelper.doRequestWithJWTAndBody(app, method, url, authToken, body);
  }

  public Result doRequestWithMultipartData(
      String method, String url, List<Part<Source<ByteString, ?>>> data, Materializer mat) {
    return FakeApiHelper.doRequestWithMultipartData(app, method, url, data, mat);
  }

  public Result doRequestWithAuthTokenAndMultipartData(
      String method,
      String url,
      String authToken,
      List<Http.MultipartFormData.Part<Source<ByteString, ?>>> data,
      Materializer mat) {
    return FakeApiHelper.doRequestWithAuthTokenAndMultipartData(
        app, method, url, authToken, data, mat);
  }

  /**
   * If you want to quickly fix existing test that returns YWError json when exception gets thrown
   * then use this function instead of Helpers.route(). Alternatively change the test to expect that
   * YWException get thrown
   */
  public Result routeWithYWErrHandler(RequestBuilder requestBuilder)
      throws InterruptedException, ExecutionException, TimeoutException {
    return FakeApiHelper.routeWithYWErrHandler(app, requestBuilder);
  }

  public Result route(RequestBuilder requestBuilder) {
    return Helpers.route(app, requestBuilder);
  }
}
