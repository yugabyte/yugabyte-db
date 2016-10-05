// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.yugabyte.yw.models.YugawareProperty;

// TODO: change the name from Configuration to YBConfiguration
public class Configuration {
  public enum Parameter {
    SupportedInstanceTypesInAWS
  }

  public static void initializeDB() {
    YugawareProperty.addConfigProperty(
        Parameter.SupportedInstanceTypesInAWS.toString(),
        "c3.xlarge,c3.2xlarge,c3.4xlarge,c3.8xlarge",
        "A comma separated list of AWS instance types can be deployed into universes.");
  }
}
