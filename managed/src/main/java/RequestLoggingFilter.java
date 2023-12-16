// Copyright (c) YugaByte, Inc.

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.logging.LogUtil;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.CompletionStage;
import java.util.function.Function;
import org.apache.pekko.stream.Materializer;
import org.slf4j.MDC;
import play.mvc.Filter;
import play.mvc.Http;
import play.mvc.Result;

/**
 * This filter is used to inject a request ID into logging events. This is used in cloud to trace
 * universe related operations all the way down to provisioning and configuration. [PLAT-3932] For
 * non-cloud deployments, a UUID is injected into the MDC for the incoming HTTP calls to trace the
 * chain of operations. The same correlation-ID is returned as a response header.
 */
@Singleton
public class RequestLoggingFilter extends Filter {
  private String requestIdHeader;

  @Inject
  public RequestLoggingFilter(Materializer mat, Config config) {
    super(mat);
    requestIdHeader = config.getString("yb.cloud.requestIdHeader");
  }

  @Override
  public CompletionStage<Result> apply(
      Function<Http.RequestHeader, CompletionStage<Result>> next, Http.RequestHeader rh) {
    String correlationId;
    Map<String, String> context = MDC.getCopyOfContextMap();
    Optional<String> optional = rh.header(this.requestIdHeader);
    if (optional.isPresent()) {
      // Internally, correlation ID is same as request ID.
      // But, correlation ID is tracked even if request ID is absent.
      // E.g background jobs are tracked with correlation ID.
      correlationId = optional.get();
      MDC.put("request-id", correlationId);
    } else {
      correlationId = UUID.randomUUID().toString();
    }
    MDC.put(LogUtil.CORRELATION_ID, correlationId);
    return next.apply(rh)
        .thenApply(
            result -> {
              if (context == null) {
                MDC.clear();
              } else {
                MDC.setContextMap(context);
              }
              return result.withHeader(this.requestIdHeader, correlationId);
            });
  }
}
