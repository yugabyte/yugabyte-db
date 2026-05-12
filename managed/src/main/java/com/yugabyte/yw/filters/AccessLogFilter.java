// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.filters;

import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import java.util.List;
import java.util.concurrent.CompletionStage;
import java.util.function.Function;
import java.util.regex.Pattern;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.MapUtils;
import org.apache.commons.lang3.StringUtils;
import org.apache.pekko.stream.Materializer;
import play.mvc.*;

@Slf4j
public class AccessLogFilter extends Filter {

  private final RuntimeConfGetter confGetter;
  private List<Pattern> excludePatterns;

  @Inject
  public AccessLogFilter(Materializer mat, RuntimeConfGetter confGetter) {
    super(mat);
    this.confGetter = confGetter;
    refreshPatterns();
  }

  public void refreshPatterns() {
    List<String> excludeFilterStrings =
        confGetter.getGlobalConf(GlobalConfKeys.accessLogExcludeRegex);
    log.debug("Audit log excludes set to {}", excludeFilterStrings);
    excludePatterns = excludeFilterStrings.stream().map(Pattern::compile).toList();
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
              String methodUri = requestHeader.method() + " " + requestHeader.uri();
              for (Pattern excludePattern : excludePatterns) {
                if (excludePattern.matcher(methodUri).matches()) {
                  return result;
                }
              }
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
