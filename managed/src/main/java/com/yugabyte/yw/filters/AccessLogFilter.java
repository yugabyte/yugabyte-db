package com.yugabyte.yw.filters;

import java.util.concurrent.CompletionStage;
import java.util.function.Function;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.MapUtils;
import org.apache.commons.lang3.StringUtils;
import org.apache.pekko.stream.Materializer;
import play.mvc.*;

@Slf4j
public class AccessLogFilter extends Filter {

  @Inject
  public AccessLogFilter(Materializer mat) {
    super(mat);
  }

  @Override
  public CompletionStage<Result> apply(
      Function<Http.RequestHeader, CompletionStage<Result>> nextFilter,
      Http.RequestHeader requestHeader) {
    long startTime = System.currentTimeMillis();
    return nextFilter
        .apply(requestHeader)
        .thenApply(
            result -> {
              long endTime = System.currentTimeMillis();
              long requestTime = endTime - startTime;

              String userAgent = requestHeader.header("User-Agent").orElse(StringUtils.EMPTY);
              String ipAddress =
                  requestHeader.header("X-FORWARDED-FOR").orElse(requestHeader.remoteAddress());
              StringBuilder queryString = new StringBuilder();
              if (MapUtils.isNotEmpty(requestHeader.queryString())) {
                requestHeader
                    .queryString()
                    .forEach(
                        (key, values) -> {
                          for (String value : values) {
                            if (queryString.isEmpty()) {
                              queryString.append("?");
                            } else {
                              queryString.append("&");
                            }
                            queryString.append(key);
                            queryString.append("=");
                            queryString.append(value);
                          }
                        });
              }
              log.info(
                  "\"{} {}{} {}\" {} {} \"{}\" \"{}\"",
                  requestHeader.method(),
                  requestHeader.uri(),
                  queryString,
                  requestHeader.version(),
                  result.status(),
                  requestTime,
                  ipAddress,
                  userAgent);

              return result.withHeader("Request-Time", "" + requestTime);
            });
  }
}
