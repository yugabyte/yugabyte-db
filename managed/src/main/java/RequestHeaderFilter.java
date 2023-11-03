// Copyright (c) YugaByte, Inc.

import static com.yugabyte.yw.commissioner.Commissioner.SUBTASK_ABORT_POSITION_PROPERTY;
import static com.yugabyte.yw.commissioner.Commissioner.SUBTASK_PAUSE_POSITION_PROPERTY;

import akka.stream.Materializer;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CompletionStage;
import java.util.function.Function;
import org.slf4j.MDC;
import play.mvc.Filter;
import play.mvc.Http;
import play.mvc.Result;

/** This filter is used to inject common headers into the request in the thread via MDC. */
@Singleton
public class RequestHeaderFilter extends Filter {

  // Register the header names here.
  private static final String[] REGISTERED_HEADERS =
      new String[] {SUBTASK_ABORT_POSITION_PROPERTY, SUBTASK_PAUSE_POSITION_PROPERTY};

  private final List<String> enabledHeaders = new ArrayList<>();

  @Inject
  public RequestHeaderFilter(Materializer mat, Config config) {
    super(mat);
    for (String header : REGISTERED_HEADERS) {
      String path = "yb.internal.headers." + header + ".enabled";
      // Read the config to find the enabled headers.
      if (config.hasPath(path) && config.getBoolean(path)) {
        enabledHeaders.add(header);
      }
    }
  }

  @Override
  public CompletionStage<Result> apply(
      Function<Http.RequestHeader, CompletionStage<Result>> next, Http.RequestHeader rh) {
    if (enabledHeaders.isEmpty()) {
      return next.apply(rh);
    }
    Map<String, String> context = MDC.getCopyOfContextMap();
    for (String header : enabledHeaders) {
      rh.header(header).ifPresent(h -> MDC.put(header, h));
    }
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
  }
}
