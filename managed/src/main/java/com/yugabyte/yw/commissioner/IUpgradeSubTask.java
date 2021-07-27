// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.List;

@FunctionalInterface
public interface IUpgradeSubTask {
  void run(List<NodeDetails> nodes, ServerType processType);
}
