// Copyright (c) Yugabyte, Inc.

package forms.commissioner;

import java.util.List;
import java.util.UUID;

import javax.validation.constraints.Size;

import play.data.validation.Constraints;

public class CreateInstanceTaskParams implements ITaskParams {
  // The instance UUID.
  @Constraints.Required()
  public UUID instanceUUID;

  // This should be a globally unique name - it is assumed to be namespaced with customer id. This
  // is used to name nodes in the instance.
  @Constraints.Required()
  public String instanceName;

  // If true, create the instance. Otherwise, edit it to match the intent.
  @Constraints.Required()
  public boolean create = true;

  // The cloud on which to create the instance.
  public String cloudProvider = "aws";

  // The nodes are evenly deployed across the different subnets.
  @Constraints.Required()
  @Size(max=3)
  public List<String> subnets;

  public int numNodes = 3;
}
