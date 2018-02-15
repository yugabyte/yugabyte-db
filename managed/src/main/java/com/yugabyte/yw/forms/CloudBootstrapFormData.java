// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import play.data.validation.Constraints;

import java.util.List;

public class CloudBootstrapFormData {
  // If this is empty, we query through ybcloud for all the available regions.
  public List<String> regionList;

  // We use hostVpcId to make sure we know where YW is coming from.
  // Not required for non-AWS deployments.
  public String hostVpcId;

  // We use destVpcId to when bootstrapping a previously existing VPC.
  public String destVpcId;
}
