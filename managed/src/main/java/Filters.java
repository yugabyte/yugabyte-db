// Copyright (c) YugaByte, Inc.

import com.google.inject.Inject;
import play.filters.cors.CORSFilter;
import play.filters.csrf.CSRFFilter;
import play.http.DefaultHttpFilters;

// TODO(sbapat): Get rid of this class and switch to config mechanism to enable filters
public class Filters extends DefaultHttpFilters {

  @Inject
  public Filters(
      CSRFFilter csrfFilter,
      CORSFilter corsFilter,
      RequestLoggingFilter requestLoggingFilter,
      RequestHeaderFilter requestHeaderFilter,
      HSTSFilter hstsFilter,
      HAApiFilter haApiFilter,
      CustomHTTPHeader customHTTPHeader,
      BlockAllRequestsFilter blockAllRequestsFilter) {
    super(
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
