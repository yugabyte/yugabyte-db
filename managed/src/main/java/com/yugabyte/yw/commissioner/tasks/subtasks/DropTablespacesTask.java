// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.TableSpaceStructures;
import com.yugabyte.yw.common.TableSpaceUtil;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Collection;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class DropTablespacesTask extends BaseTablespacesTask {
  private static final int RETRIES = 3;

  public static final String SQL = "DROP TABLESPACE %s";

  private final NodeUniverseManager nodeUniverseManager;

  @Inject
  protected DropTablespacesTask(
      BaseTaskDependencies baseTaskDependencies, NodeUniverseManager nodeUniverseManager) {
    super(baseTaskDependencies);
    this.nodeUniverseManager = nodeUniverseManager;
  }

  public static class Params extends UniverseTaskParams {
    public Collection<String> tablespaceNames;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    log.info("Running {}", getName());
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());

    Map<String, TableSpaceStructures.TableSpaceInfo> current = getTablespaces(universe, null);
    Set<String> tablespaces = new HashSet<>(taskParams().tablespaceNames);
    tablespaces.retainAll(current.keySet());
    int attempt = 0;
    Set<String> processed = new HashSet<>();
    while (processed.size() < tablespaces.size() && attempt++ < RETRIES) {
      try {
        NodeDetails randomTserver = getRandomTserver(universe);
        for (String tablespace : tablespaces) {
          String query = String.format(SQL, tablespace);
          nodeUniverseManager
              .runYsqlCommand(randomTserver, universe, TableSpaceUtil.DB, query)
              .processErrors();
          processed.add(tablespace);
          log.debug("Dropped tablespace {}", tablespace);
        }
      } catch (Exception e) {
        log.error("Failed to move tables", e);
      }
    }
  }
}
