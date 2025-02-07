// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.YsqlQueryExecutor;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.RandomStringUtils;

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
    DELETE_PG_PASS_FILE
  }

  public static class Params extends UniverseTaskParams {
    public Action action;
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
      createUser(universe, masterLeaderNode, pgPassFilePath);
    } else if (taskParams().action == Action.DELETE_USER) {
      dropUser(universe, masterLeaderNode, pgPassFilePath);
      deletePGPassFile(universe, masterLeaderNode, pgPassFilePath);
    } else if (taskParams().action == Action.DELETE_PG_PASS_FILE) {
      deletePGPassFile(universe, masterLeaderNode, pgPassFilePath);
    }
  }

  private void dropUser(Universe universe, NodeDetails node, String pgPassFilePath) {
    ysqlQueryExecutor.runUserDbCommands(
        "DROP USER IF EXISTS " + UPGRADE_SUPERUSER, "template1", universe);
  }

  private void deletePGPassFile(Universe universe, NodeDetails node, String pgPassFilePath) {
    nodeUniverseManager.runCommand(node, universe, ImmutableList.of("rm", "-f", pgPassFilePath));
  }

  private void createUser(Universe universe, NodeDetails node, String pgPassFilePath) {
    String allowedCharsInPassword =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    for (String charString : Util.SPECIAL_CHARACTERS_STRING_LIST) {
      // Avoid using % in the password.
      if (!charString.equals("%")) {
        allowedCharsInPassword += charString;
      }
    }
    String password = RandomStringUtils.secureStrong().next(20, allowedCharsInPassword);
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
    String pgPassFileContent = "*:*:*:" + UPGRADE_SUPERUSER + ":" + password;
    ShellProcessContext context =
        ShellProcessContext.builder()
            .logCmdOutput(false)
            .redactedVals(
                ImmutableMap.of(pgPassFileContent, "*:*:*:REDACTED_USERNAME:REDACTED_PASSWORD"))
            .build();
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
        context);
  }
}
