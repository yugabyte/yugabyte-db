// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.forms;

import play.data.validation.Constraints;

/**
 * This class will be used by the API and UI Form Elements to validate constraints for PlacementRegion Data
 */
public class RegionFormData {
	@Constraints.Required()
	@Constraints.MinLength(5)
	public String code;

	@Constraints.Required()
	@Constraints.MinLength(5)
	public String name;
}
