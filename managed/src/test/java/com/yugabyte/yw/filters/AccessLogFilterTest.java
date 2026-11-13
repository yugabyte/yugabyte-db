// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.filters;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.mvc.Results.forbidden;
import static play.mvc.Results.ok;

import ch.qos.logback.classic.Logger;
import ch.qos.logback.classic.spi.ILoggingEvent;
import ch.qos.logback.core.AppenderBase;
import com.typesafe.config.Config;
import com.typesafe.config.ConfigFactory;
import com.typesafe.config.ConfigValueFactory;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.logging.LogUtil;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;
import org.apache.pekko.actor.ActorSystem;
import org.apache.pekko.stream.Materializer;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.slf4j.LoggerFactory;
import org.slf4j.MDC;
import play.mvc.Http;
import play.mvc.Result;

public class AccessLogFilterTest {
  private static final String HEADER = "X-REQUEST-ID";

  private ActorSystem actorSystem;
  private AccessLogFilter accessLogFilter;
  private RequestLoggingFilter requestLoggingFilter;
  private Logger logger;
  // MDC snapshot of each access log event, taken when the event is logged.
  private final List<Map<String, String>> loggedMdc = new ArrayList<>();
  private final AppenderBase<ILoggingEvent> appender =
      new AppenderBase<>() {
        @Override
        protected void append(ILoggingEvent event) {
          loggedMdc.add(event.getMDCPropertyMap());
        }
      };

  @Before
  public void setup() {
    actorSystem = ActorSystem.create();
    Materializer materializer = Materializer.matFromSystem(actorSystem);
    Config config =
        ConfigFactory.empty()
            .withValue("yb.cloud.requestIdHeader", ConfigValueFactory.fromAnyRef(HEADER));
    RuntimeConfGetter confGetter = mock(RuntimeConfGetter.class);
    when(confGetter.getGlobalConf(any())).thenReturn(Collections.emptyList());
    accessLogFilter = new AccessLogFilter(materializer, confGetter, config);
    requestLoggingFilter = new RequestLoggingFilter(materializer, config);
    logger = (Logger) LoggerFactory.getLogger(AccessLogFilter.class);
    appender.start();
    logger.addAppender(appender);
    MDC.clear();
  }

  @After
  public void teardown() {
    logger.detachAppender(appender);
    actorSystem.terminate();
  }

  // Same nesting as Filters: AccessLogFilter wraps RequestLoggingFilter.
  private Result runRequest(Http.RequestHeader rh) throws Exception {
    return accessLogFilter
        .apply(
            h1 -> requestLoggingFilter.apply(h2 -> CompletableFuture.completedFuture(ok("ok")), h1),
            rh)
        .toCompletableFuture()
        .get(10, TimeUnit.SECONDS);
  }

  @Test
  public void testRequestIdLogged() throws Exception {
    MDC.put("other", "kept");
    Http.RequestHeader rh = new Http.RequestBuilder().header(HEADER, "req-123").build();
    runRequest(rh);

    assertEquals(1, loggedMdc.size());
    assertEquals("req-123", loggedMdc.get(0).get(LogUtil.REQUEST_ID));
    assertEquals("req-123", loggedMdc.get(0).get(LogUtil.CORRELATION_ID));
    assertNull(MDC.get(LogUtil.REQUEST_ID));
    assertEquals("kept", MDC.get("other"));
  }

  // An earlier filter (e.g. CSRF) rejects the request before RequestLoggingFilter runs.
  @Test
  public void testRequestIdLoggedWhenRejectedEarly() throws Exception {
    Http.RequestHeader rh = new Http.RequestBuilder().header(HEADER, "req-123").build();
    accessLogFilter
        .apply(h -> CompletableFuture.completedFuture(forbidden()), rh)
        .toCompletableFuture()
        .get(10, TimeUnit.SECONDS);

    assertEquals(1, loggedMdc.size());
    assertEquals("req-123", loggedMdc.get(0).get(LogUtil.REQUEST_ID));
    assertEquals("req-123", loggedMdc.get(0).get(LogUtil.CORRELATION_ID));
  }

  @Test
  public void testGeneratedCorrelationIdLogged() throws Exception {
    Result result = runRequest(new Http.RequestBuilder().build());

    assertEquals(1, loggedMdc.size());
    String correlationId = result.header(HEADER).orElseThrow();
    assertFalse(loggedMdc.get(0).containsKey(LogUtil.REQUEST_ID));
    assertEquals(correlationId, loggedMdc.get(0).get(LogUtil.CORRELATION_ID));
    assertNull(MDC.get(LogUtil.CORRELATION_ID));
  }
}
