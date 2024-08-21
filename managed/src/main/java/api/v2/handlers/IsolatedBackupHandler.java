package api.v2.handlers;

import api.v2.models.IsolatedBackupCreateSpec;
import api.v2.models.IsolatedBackupRestoreSpec;
import api.v2.models.YBATask;
import api.v2.utils.ApiControllerUtils;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.tasks.CreateYbaBackup;
import com.yugabyte.yw.commissioner.tasks.RestoreYbaBackup;
import com.yugabyte.yw.models.Customer;
import java.util.UUID;
import play.mvc.Http;

public class IsolatedBackupHandler extends ApiControllerUtils {

  @Inject private YbaBackupHandler ybaBackupHandler;

  public YBATask createYbaBackup(Http.Request request, UUID cUUID, IsolatedBackupCreateSpec spec)
      throws Exception {
    Customer customer = Customer.getOrBadRequest(cUUID);
    CreateYbaBackup.Params taskParams = new CreateYbaBackup.Params();
    taskParams.localDir = spec.getLocalDir();
    taskParams.components = spec.getComponents();
    UUID taskUUID = ybaBackupHandler.createBackup(customer, taskParams);
    return new YBATask().taskUuid(taskUUID);
  }

  public YBATask restoreYbaBackup(Http.Request request, UUID cUUID, IsolatedBackupRestoreSpec spec)
      throws Exception {
    Customer customer = Customer.getOrBadRequest(cUUID);
    RestoreYbaBackup.Params taskParams = new RestoreYbaBackup.Params();
    taskParams.localPath = spec.getLocalPath();
    UUID taskUUID = ybaBackupHandler.restoreBackup(customer, taskParams);
    return new YBATask().taskUuid(taskUUID);
  }
}
