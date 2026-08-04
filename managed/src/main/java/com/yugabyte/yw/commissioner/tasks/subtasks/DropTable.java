// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import java.time.Duration;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.CommonTypes;

@Slf4j
public class DropTable extends UniverseTaskBase {
  @Inject
  protected DropTable(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  private static long WAIT_TIME_IN_MILLIS = 5000;

  public static class Params extends UniverseTaskParams {
    public String dbName;
    public String tableName;
    public CommonTypes.TableType tableType;
  }

  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().getUniverseUUID() + ")";
  }

  @Override
  public void run() {
    try {
      log.info("Running {}", getName());
      Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
      if (taskParams().tableType.equals(CommonTypes.TableType.PGSQL_TABLE_TYPE)) {
        try {
          ysqlQueryExecutor.dropTable(universe, taskParams().dbName, taskParams().tableName);
          waitFor(Duration.ofMillis(WAIT_TIME_IN_MILLIS));
        } catch (PlatformServiceException e) {
          log.error("Error dropping table: " + e.getMessage());
          throw e;
        }
      } else if (taskParams().tableType.equals(CommonTypes.TableType.YQL_TABLE_TYPE)) {
        throw new UnsupportedOperationException(
            "Un-implemented table type: " + taskParams().tableType);
      } else {
        throw new IllegalArgumentException("Unsupported table type: " + taskParams().tableType);
      }
    } catch (Exception e) {
      String msg = getName() + " failed with exception " + e.getMessage();
      log.warn(msg, e.getMessage());
      throw new RuntimeException(msg, e);
    }
    log.info("Completed {}", getName());
  }
}
