// Copyright (c) YugaByte, Inc.

import com.google.inject.Inject;
import play.mvc.EssentialFilter;
import play.filters.cors.CORSFilter;
import play.http.HttpFilters;

public class Filters implements HttpFilters {

  @Inject
  CORSFilter corsFilter;

  public EssentialFilter[] filters() {
    return new EssentialFilter[] { corsFilter.asJava() };
  }
}
