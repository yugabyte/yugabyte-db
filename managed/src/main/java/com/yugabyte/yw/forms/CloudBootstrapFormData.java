// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import play.data.validation.Constraints;

import java.util.List;

public class CloudBootstrapFormData {
  @Constraints.Required()
  public List<String> regionList;

  // We use hostVPCId to enable VPC peering.
  public String hostVPCId;
}
