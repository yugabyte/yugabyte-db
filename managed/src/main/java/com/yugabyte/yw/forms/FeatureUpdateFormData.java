// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.databind.JsonNode;
import play.data.validation.Constraints;

/**
 * This class will be used by the API and UI Form Elements to validate constraints for
 * CloudProvider
 */
public class FeatureUpdateFormData {
    @Constraints.Required()
    public JsonNode features;
}
