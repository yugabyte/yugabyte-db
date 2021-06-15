package com.yugabyte.yw.common.swagger;

import io.swagger.core.filter.AbstractSpecFilter;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class YWSwaggerSpecFilter extends AbstractSpecFilter {
  public static final Logger LOG = LoggerFactory.getLogger(YWSwaggerSpecFilter.class);

  public YWSwaggerSpecFilter() {
    YWModelConverter.register();
  }
}
