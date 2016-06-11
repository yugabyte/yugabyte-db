// Copyright (c) Yugabyte, Inc.

package forms.commissioner;

import java.util.UUID;

import play.data.validation.Constraints;

public class CreateInstanceTaskParams implements ITaskParams {
  @Constraints.Required()
  public UUID instanceUUID;

  @Constraints.Required()
  public String instanceName;
}
