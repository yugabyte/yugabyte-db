package com.yugabyte.yw.models.common;

public @interface KeyFromURI {
  int[] pathIndices();

  String[] queryParams() default {};
}
