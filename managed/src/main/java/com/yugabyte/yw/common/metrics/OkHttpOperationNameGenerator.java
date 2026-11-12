// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.metrics;

import kamon.instrumentation.http.HttpMessage;
import kamon.instrumentation.http.HttpOperationNameGenerator;
import scala.Option;

/**
 * Names OkHttp client spans by URL without the query string. Kamon's default uses the full URL, so
 * every distinct query adds new span_processing_time_seconds series that are never dropped.
 */
public class OkHttpOperationNameGenerator implements HttpOperationNameGenerator {

  @Override
  public Option<String> name(HttpMessage.Request request) {
    String url = request.url();
    if (url == null) {
      return Option.empty();
    }
    int end = url.indexOf('?');
    if (end < 0) {
      end = url.indexOf('#');
    }
    return Option.apply(end < 0 ? url : url.substring(0, end));
  }
}
