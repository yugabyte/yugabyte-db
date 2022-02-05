// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.helpers.BundleDetails.ComponentType;
import java.util.Date;
import java.util.EnumSet;

public class SupportBundleFormData {
  public Date startDate;

  public Date endDate;

  public EnumSet<ComponentType> components;
}
