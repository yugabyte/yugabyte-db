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

  // The number of nodes to provision.
  public int numNodes = 3;

  // The software version of YB to install.
  // TODO: replace with a nicer string as default.
  public String ybServerPkg =
      "yb-server-0.0.1-SNAPSHOT.3a3bc5896e8842bfba683394c2c863a33e3cc4c1.tar.gz";
}
