// Copyright (c) Yugabyte, Inc.

package forms.cloud;

import play.data.validation.Constraints;

/**
 * This class will be used by the API and UI Form Elements to validate constraints for Region Data
 */
public class AvailabilityZoneFormData {
	@Constraints.Required()
	@Constraints.MinLength(5)
	public String code;

	@Constraints.Required()
	@Constraints.MinLength(5)
	public String name;

	@Constraints.Required()
	@Constraints.MinLength(5)
	public String subnet;
}
