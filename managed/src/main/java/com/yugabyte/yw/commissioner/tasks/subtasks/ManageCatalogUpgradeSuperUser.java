// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.YsqlQueryExecutor;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class ManageCatalogUpgradeSuperUser extends UniverseTaskBase {

  private final NodeUniverseManager nodeUniverseManager;
  private final YsqlQueryExecutor ysqlQueryExecutor;

  private static final String UPGRADE_SUPERUSER = "yugabyte_upgrade";

  @Inject
  protected ManageCatalogUpgradeSuperUser(
      BaseTaskDependencies baseTaskDependencies,
      NodeUniverseManager nodeUniverseManager,
      YsqlQueryExecutor ysqlQueryExecutor) {
    super(baseTaskDependencies);
    this.nodeUniverseManager = nodeUniverseManager;
    this.ysqlQueryExecutor = ysqlQueryExecutor;
  }

  public enum Action {
    CREATE_USER,
    DELETE_USER,
    CREATE_USER_AND_PG_PASS_FILE,
    CREATE_PG_PASS_FILE,
    DELETE_PG_PASS_FILE
  }

  public static class Params extends UniverseTaskParams {
    public Action action;
    public String password;
  }

  public Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    Universe universe = getUniverse();
    NodeDetails masterLeaderNode = universe.getMasterLeaderNode();
    String pgPassFilePath =
        Util.getNodeHomeDir(universe.getUniverseUUID(), universe.getMasterLeaderNode())
            + "/.pgpass";
    if (taskParams().action == Action.CREATE_USER) {
      dropUser(universe, masterLeaderNode, pgPassFilePath);
      deletePGPassFile(universe, masterLeaderNode, pgPassFilePath);
      createUser(universe, masterLeaderNode, pgPassFilePath, taskParams().password);
    } else if (taskParams().action == Action.DELETE_USER) {
      dropUser(universe, masterLeaderNode, pgPassFilePath);
      deletePGPassFile(universe, masterLeaderNode, pgPassFilePath);
    } else if (taskParams().action == Action.CREATE_USER_AND_PG_PASS_FILE) {
      dropUser(universe, masterLeaderNode, pgPassFilePath);
      deletePGPassFile(universe, masterLeaderNode, pgPassFilePath);
      createUser(universe, masterLeaderNode, pgPassFilePath, taskParams().password);
      createPGPassFile(universe, masterLeaderNode, pgPassFilePath, taskParams().password);
    } else if (taskParams().action == Action.DELETE_PG_PASS_FILE) {
      deletePGPassFile(universe, masterLeaderNode, pgPassFilePath);
    } else if (taskParams().action == Action.CREATE_PG_PASS_FILE) {
      createPGPassFile(universe, masterLeaderNode, pgPassFilePath, taskParams().password);
    }
  }

  private void dropUser(Universe universe, NodeDetails node, String pgPassFilePath) {
    ysqlQueryExecutor.runUserDbCommands(
        "DROP USER IF EXISTS " + UPGRADE_SUPERUSER, "template1", universe);
  }

  private void deletePGPassFile(Universe universe, NodeDetails node, String pgPassFilePath) {
    nodeUniverseManager.runCommand(node, universe, ImmutableList.of("rm", "-f", pgPassFilePath));
  }

  private void createUser(
      Universe universe, NodeDetails node, String pgPassFilePath, String password) {
    String query =
        String.format(
            """
    DO $$DECLARE time TIMESTAMP := now() + INTERVAL '4 HOURS';
    BEGIN
    EXECUTE format('CREATE ROLE %s WITH SUPERUSER LOGIN PASSWORD ''%s'' VALID UNTIL ''%s'';', time);
    END$$;
    """,
            UPGRADE_SUPERUSER, password, "%s");
    ysqlQueryExecutor.runUserDbCommands(query, "template1", universe);
  }

  private void createPGPassFile(
      Universe universe, NodeDetails node, String pgPassFilePath, String password) {
    String pgPassFileContent = "*:*:*:" + UPGRADE_SUPERUSER + ":" + password;
    if (universe
        .getUniverseDetails()
        .getPrimaryCluster()
        .userIntent
        .providerType
        .equals(CloudType.kubernetes)) {
      String command =
          "rm -rf "
              + pgPassFilePath
              + "; echo "
              + pgPassFileContent
              + " >> "
              + pgPassFilePath
              + "; chmod 600 "
              + pgPassFilePath;
      String redactedCommand =
          "rm -rf "
              + pgPassFilePath
              + "; echo *:*:*:REDACTED_USERNAME:REDACTED_PASSWORD >> "
              + pgPassFilePath
              + "; chmod 600 "
              + pgPassFilePath;
      command = command.replace("$", "\\$");
      nodeUniverseManager.runCommand(
          node,
          universe,
          ImmutableList.of("/bin/bash", "-c", command),
          ShellProcessContext.builder()
              .logCmdOutput(false)
              .redactedVals(ImmutableMap.of(command, redactedCommand))
              .build());
    } else {
      nodeUniverseManager.runCommand(
          node,
          universe,
          ImmutableList.of(
              "rm",
              "-rf",
              pgPassFilePath,
              ";",
              "echo",
              pgPassFileContent,
              ">>",
              pgPassFilePath,
              ";",
              "chmod",
              "600",
              pgPassFilePath),
          ShellProcessContext.builder()
              .logCmdOutput(false)
              .redactedVals(
                  ImmutableMap.of(pgPassFileContent, "*:*:*:REDACTED_USERNAME:REDACTED_PASSWORD"))
              .build());
    }
  }
}
