package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.forms.TableTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.YBClient;

@Slf4j
public class DeleteTablesFromUniverse extends AbstractTaskBase {

  @Inject
  protected DeleteTablesFromUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends TableTaskParams {
    // The universe UUID must be stored in universeUUID field.
    // A map from keyspace to tables' names containing the tables to be dropped.
    public Map<String, List<String>> keyspaceTablesMap;

    public String getFullTablesNameListToDelete() {
      List<String> fullTableNames = new ArrayList<>();
      keyspaceTablesMap.forEach(
          (keyspace, tableNames) ->
              tableNames.forEach(
                  tableName -> fullTableNames.add(getTableFullName(keyspace, tableName))));
      return fullTableNames.toString();
    }
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s (Universe=%s, fullTableNames=%s)",
        super.getName(),
        taskParams().getUniverseUUID(),
        taskParams().getFullTablesNameListToDelete());
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    Map<String, List<String>> keyspaceTablesMap = taskParams().keyspaceTablesMap;
    if (keyspaceTablesMap == null) {
      throw new RuntimeException("taskParams().keyspaceTablesMap cannot be null");
    }

    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    String universeMasterAddresses = universe.getMasterAddresses();
    String universeCertificate = universe.getCertificateNodetoNode();
    try (YBClient client = ybService.getClient(universeMasterAddresses, universeCertificate)) {
      keyspaceTablesMap.forEach(
          (keyspace, tableNames) ->
              tableNames.forEach(
                  tableName -> {
                    try {
                      client.deleteTable(keyspace, tableName);
                      log.info(
                          "Dropped table {}",
                          CommonUtils.logTableName(getTableFullName(keyspace, tableName)));
                    } catch (Exception e) {
                      String errMsg =
                          String.format(
                              "Failed to drop table %s: %s",
                              CommonUtils.logTableName(getTableFullName(keyspace, tableName)),
                              e.getMessage());
                      log.error(errMsg, e);
                      throw new RuntimeException(errMsg);
                    }
                  }));
    } catch (Exception e) {
      String errMsg = String.format("Failed to drop tables: %s", e.getMessage());
      log.error(errMsg, e);
      throw new RuntimeException(errMsg);
    }

    log.info("Completed {}", getName());
  }

  private static String getTableFullName(String keyspace, String tableName) {
    return keyspace + "." + tableName;
  }
}
