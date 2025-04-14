package api.v2.handlers;

import api.v2.models.IsolatedBackupCreateSpec;
import api.v2.models.IsolatedBackupRestoreSpec;
import api.v2.models.YBATask;
import api.v2.utils.ApiControllerUtils;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.CreateYbaBackup;
import com.yugabyte.yw.commissioner.tasks.RestoreYbaBackup;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.UUID;
import play.mvc.Http;

public class IsolatedBackupHandler extends ApiControllerUtils {

  @Inject private Commissioner commissioner;
  @Inject private ConfigHelper configHelper;

  public YBATask createYbaBackup(Http.Request request, UUID cUUID, IsolatedBackupCreateSpec spec)
      throws Exception {
    Customer customer = Customer.getOrBadRequest(cUUID);
    CreateYbaBackup.Params taskParams = new CreateYbaBackup.Params();
    taskParams.localPath = spec.getLocalDir();
    taskParams.components = spec.getComponents();
    UUID taskUUID = commissioner.submit(TaskType.CreateYbaBackup, taskParams);
    CustomerTask.create(
        customer,
        configHelper.getYugawareUUID(),
        taskUUID,
        CustomerTask.TargetType.Yba,
        CustomerTask.TaskType.CreateYbaBackup,
        Util.getYwHostnameOrIP());
    return new YBATask().taskUuid(taskUUID);
  }

  public YBATask restoreYbaBackup(Http.Request request, UUID cUUID, IsolatedBackupRestoreSpec spec)
      throws Exception {
    Customer customer = Customer.getOrBadRequest(cUUID);
    RestoreYbaBackup.Params taskParams = new RestoreYbaBackup.Params();
    taskParams.localPath = spec.getLocalPath();
    UUID taskUUID = commissioner.submit(TaskType.RestoreYbaBackup, taskParams);
    CustomerTask.create(
        customer,
        configHelper.getYugawareUUID(),
        taskUUID,
        CustomerTask.TargetType.Yba,
        CustomerTask.TaskType.RestoreYbaBackup,
        Util.getYwHostnameOrIP());
    return new YBATask().taskUuid(taskUUID);
  }
}
