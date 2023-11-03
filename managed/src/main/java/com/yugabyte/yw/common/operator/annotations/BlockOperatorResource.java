// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.operator.annotations;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;
import play.mvc.With;

@With(BlockOperatorResourceHandler.class)
@Target(ElementType.METHOD)
@Retention(RetentionPolicy.RUNTIME)
public @interface BlockOperatorResource {
  OperatorResourceTypes resource();
}
