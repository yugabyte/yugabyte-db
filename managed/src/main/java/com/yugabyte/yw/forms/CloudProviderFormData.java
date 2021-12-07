// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonSetter;
import com.fasterxml.jackson.annotation.Nulls;
import com.yugabyte.yw.commissioner.Common;
import java.util.HashMap;
import java.util.Map;
import play.data.validation.Constraints;

/**
 * This class will be used by the API and UI Form Elements to validate constraints for CloudProvider
 */
public class CloudProviderFormData {
  @Constraints.Required() public Common.CloudType code;

  @Constraints.Required() public String name;

  // We would store credentials and other environment
  // settings specific to the provider as a key-value map.
  @JsonSetter(nulls = Nulls.AS_EMPTY)
  public Map<String, String> config = new HashMap<>();

  public String region = null;
}
