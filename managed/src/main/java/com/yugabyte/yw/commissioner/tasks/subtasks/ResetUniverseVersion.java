// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.common.collect.Sets;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.models.AsyncReplicationRelationship;
import com.yugabyte.yw.models.Universe;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.cdc.CdcConsumer;
import org.yb.client.YBClient;
import org.yb.master.Master;
import play.api.Play;

import javax.inject.Inject;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.UUID;

public class ResetUniverseVersion extends UniverseDefinitionTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(ResetUniverseVersion.class);

  @Inject
  protected ResetUniverseVersion(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void initialize(ITaskParams params) {
    super.initialize(params);
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
    universe.resetVersion();
  }
}
