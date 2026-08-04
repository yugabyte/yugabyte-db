package com.yugabyte.yw.filters; // Copyright (c) YugabyteDB, Inc.

import com.google.inject.Inject;
import play.filters.cors.CORSFilter;
import play.filters.csrf.CSRFFilter;
import play.http.DefaultHttpFilters;

public class Filters extends DefaultHttpFilters {

  @Inject
  public Filters(
      AccessLogFilter accessLogFilter,
      CSRFFilter csrfFilter,
      CORSFilter corsFilter,
      RequestLoggingFilter requestLoggingFilter,
      RequestHeaderFilter requestHeaderFilter,
      HSTSFilter hstsFilter,
      HAApiFilter haApiFilter,
      CustomHTTPHeader customHTTPHeader,
      BlockAllRequestsFilter blockAllRequestsFilter) {
    super(
        accessLogFilter,
        corsFilter,
        csrfFilter,
        requestLoggingFilter,
        requestHeaderFilter,
        hstsFilter,
        haApiFilter,
        customHTTPHeader,
        blockAllRequestsFilter);
  }
}
