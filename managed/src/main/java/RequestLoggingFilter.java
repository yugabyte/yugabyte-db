import akka.stream.Materializer;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import org.slf4j.MDC;
import play.mvc.Filter;
import play.mvc.Http;
import play.mvc.Result;

import java.util.Map;
import java.util.concurrent.CompletionStage;
import java.util.function.Function;
import com.typesafe.config.Config;

/**
 * This filter is used to inject a request ID into logging events. This is used in cloud to trace
 * universe related operations all the way down to provisioning and configuration. Currently it's a
 * no-op for non-cloud deployments.
 */
@Singleton
public class RequestLoggingFilter extends Filter {
  private String requestIdHeader;

  @Inject
  public RequestLoggingFilter(Materializer mat, Config config) {
    super(mat);

    if (config.getBoolean("yb.cloud.enabled")) {
      requestIdHeader = config.getString("yb.cloud.requestIdHeader");
    }
  }

  @Override
  public CompletionStage<Result> apply(
      Function<Http.RequestHeader, CompletionStage<Result>> next, Http.RequestHeader rh) {
    if (this.requestIdHeader != null) {
      Map<String, String> context = MDC.getCopyOfContextMap();
      rh.header(this.requestIdHeader).ifPresent(h -> MDC.put("request-id", h));
      return next.apply(rh)
          .thenApply(
              result -> {
                if (context == null) {
                  MDC.clear();
                } else {
                  MDC.setContextMap(context);
                }
                return result;
              });
    } else {
      return next.apply(rh);
    }
  }
}
