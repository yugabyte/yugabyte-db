// Copyright (c) YugaByte, Inc.

package forms.commissioner;

import java.util.List;
import java.util.UUID;

import javax.validation.constraints.Size;

import play.data.validation.Constraints;

public class UniverseTaskParams extends TaskParamsBase {
  // This should be a globally unique name - it is a combination of the customer id and the universe
  // id. This is used as the prefix of name nodes in the universe.
  @Constraints.Required()
  public String nodePrefix;

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

  // The instance UUID which is filled in from the URL path.
  public UUID universeUUID;
}
