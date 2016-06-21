// Copyright (c) YugaByte, Inc.

package forms.yb;

import com.fasterxml.jackson.databind.JsonNode;
import models.yb.Instance;
import play.data.validation.Constraints;

import javax.validation.Constraint;
import java.util.UUID;

/**
 * This class will be used by the API and UI Form Elements to validate constraints for yb Instance Data
 */

public class CreateInstanceFormData {
	@Constraints.Required()
	public String name;

	@Constraints.Required()
	public UUID regionUUID;

	@Constraints.Required()
	public Boolean multiAZ;

	@Constraints.Required()
	@Constraints.Min(3)
	public int replicationFactor = 3;
}
