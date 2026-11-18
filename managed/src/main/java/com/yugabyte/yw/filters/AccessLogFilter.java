// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.filters;

import com.typesafe.config.Config;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.logging.LogUtil;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletionStage;
import java.util.function.Function;
import java.util.regex.Pattern;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.MapUtils;
import org.apache.commons.lang3.StringUtils;
import org.apache.pekko.stream.Materializer;
import org.slf4j.MDC;
import play.mvc.*;

@Slf4j
public class AccessLogFilter extends Filter {

  private final RuntimeConfGetter confGetter;
  private final String requestIdHeader;
  private List<Pattern> excludePatterns;

  @Inject
  public AccessLogFilter(Materializer mat, RuntimeConfGetter confGetter, Config config) {
    super(mat);
    this.confGetter = confGetter;
    this.requestIdHeader = config.getString("yb.cloud.requestIdHeader");
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
              // RequestLoggingFilter runs inside this filter and has already reset the MDC, so
              // re-add the IDs. The response lacks the header if an earlier filter rejected it.
              Optional<String> requestId = requestHeader.header(requestIdHeader);
              Optional<String> correlationId = result.header(requestIdHeader).or(() -> requestId);
              Map<String, String> mdcContext = MDC.getCopyOfContextMap();
              try {
                requestId.ifPresent(id -> MDC.put(LogUtil.REQUEST_ID, id));
                correlationId.ifPresent(id -> MDC.put(LogUtil.CORRELATION_ID, id));
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
              } finally {
                if (mdcContext == null) {
                  MDC.clear();
                } else {
                  MDC.setContextMap(mdcContext);
                }
              }

              return result.withHeader("Request-Time", "" + requestTime);
            });
  }
}
