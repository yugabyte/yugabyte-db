// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.commissioner.Common;
import play.data.validation.Constraints;

import java.util.Map;


/**
 * This class will be used by the API and UI Form Elements to validate constraints for
 * CloudProvider
 */
public class CloudProviderFormData {
    @Constraints.Required()
    public Common.CloudType code;

    @Constraints.Required()
    public String name;

    public Boolean active = true;

    // We would store credentials and other environment
    // settings specific to the provider as a key-value map.
    public Map<String, String> config;

    public String region = null;
}
