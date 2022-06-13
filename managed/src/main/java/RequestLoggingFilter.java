import akka.stream.Materializer;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.logging.LogUtil;
import org.slf4j.MDC;
import play.mvc.Filter;
import play.mvc.Http;
import play.mvc.Result;

import java.util.Map;
import java.util.UUID;
import java.util.concurrent.CompletionStage;
import java.util.function.Function;
import com.typesafe.config.Config;

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
    Map<String, String> context = MDC.getCopyOfContextMap();
    rh.header(this.requestIdHeader).ifPresent(h -> MDC.put("request-id", h));
    String correlationId =
        rh.header(this.requestIdHeader).isPresent()
            ? rh.header(this.requestIdHeader).get()
            : UUID.randomUUID().toString();
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
