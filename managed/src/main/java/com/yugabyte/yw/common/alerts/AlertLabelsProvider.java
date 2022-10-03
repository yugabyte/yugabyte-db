// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import java.util.UUID;

public interface AlertLabelsProvider {
  String getLabelValue(String name);

  String getAnnotationValue(String name);

  UUID getUuid();
}
