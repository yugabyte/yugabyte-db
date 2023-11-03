// Copyright (c) Yugabyte, Inc.
package com.yugabyte.yw.rbac.annotations;

import com.yugabyte.yw.rbac.enums.SourceType;
import io.ebean.Model;

public @interface Resource {
  String path();

  SourceType sourceType();

  Class<? extends Model> dbClass() default Model.class;

  // Needed only in the case of SourceType.DB
  String identifier() default "";

  // Needed only in the case of SourceType.DB
  String columnName() default "";
}
