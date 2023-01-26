// Copyright (c) YugaByte, Inc.

import com.google.inject.Inject;
import play.filters.cors.CORSFilter;
import play.filters.csrf.CSRFFilter;
import play.filters.gzip.GzipFilter;
import play.filters.https.RedirectHttpsFilter;
import play.http.DefaultHttpFilters;

// TODO(sbapat): Get rid of this class an switch to config mechanism to enable filters
public class Filters extends DefaultHttpFilters {

  @Inject
  public Filters(
      CSRFFilter csrfFilter,
      CORSFilter corsFilter,
      RequestLoggingFilter requestLoggingFilter,
      RequestHeaderFilter requestHeaderFilter,
      GzipFilter gzipFilter,
      RedirectHttpsFilter redirectHttpsFilter) {
    super(
        corsFilter,
        csrfFilter,
        requestLoggingFilter,
        requestHeaderFilter,
        gzipFilter,
        redirectHttpsFilter);
  }
}
