package api.v2.handlers;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.NOT_FOUND;

import api.v2.mappers.ContinuousBackupMapper;
import api.v2.models.ContinuousBackup;
import api.v2.models.ContinuousBackupCreateSpec;
import api.v2.models.ContinuousRestoreSpec;
import api.v2.models.YBATask;
import api.v2.utils.ApiControllerUtils;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.tasks.RestoreContinuousBackup;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.ContinuousBackupConfig;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.helpers.TimeUnit;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import play.mvc.Http;

public class ContinuousBackupHandler extends ApiControllerUtils {

  @Inject private YbaBackupHandler ybaBackupHandler;

  public ContinuousBackup createContinuousBackup(
      Http.Request request, UUID cUUID, ContinuousBackupCreateSpec continuousBackupCreateSpec)
      throws Exception {

    ContinuousBackupConfig cbConfig =
        ContinuousBackupConfig.create(
            continuousBackupCreateSpec.getStorageConfigUuid(),
            continuousBackupCreateSpec.getFrequency(),
            TimeUnit.valueOf(continuousBackupCreateSpec.getFrequencyTimeUnit().name()),
            continuousBackupCreateSpec.getNumBackups(),
            continuousBackupCreateSpec.getBackupDir());
    return ContinuousBackupMapper.INSTANCE.toContinuousBackup(cbConfig);
  }

  public ContinuousBackup deleteContinuousBackup(Http.Request request, UUID cUUID, UUID bUUID)
      throws Exception {
    Optional<ContinuousBackupConfig> optional = ContinuousBackupConfig.get(bUUID);
    if (!optional.isPresent()) {
      throw new PlatformServiceException(BAD_REQUEST, "no continous backup config found with UUID");
    }
    ContinuousBackupConfig.delete(bUUID);
    return new ContinuousBackup();
  }

  public ContinuousBackup editContinuousBackup(
      Http.Request request,
      UUID cUUID,
      UUID bUUID,
      ContinuousBackupCreateSpec continuousBackupCreateSpec)
      throws Exception {
    Optional<ContinuousBackupConfig> optional = ContinuousBackupConfig.get(bUUID);
    if (!optional.isPresent()) {
      throw new PlatformServiceException(BAD_REQUEST, "no continous backup config found with UUID");
    }
    ContinuousBackupConfig cbConfig = optional.get();
    // TODO: Actual edit work
    return ContinuousBackupMapper.INSTANCE.toContinuousBackup(cbConfig);
  }

  public ContinuousBackup getContinuousBackup(Http.Request request, UUID cUUID) throws Exception {
    List<ContinuousBackupConfig> cbConfigs = ContinuousBackupConfig.getAll();
    if (cbConfigs.size() < 1) {
      throw new PlatformServiceException(NOT_FOUND, "No continuous backup config found.");
    }
    ContinuousBackupConfig cbConfig = cbConfigs.get(0);
    return ContinuousBackupMapper.INSTANCE.toContinuousBackup(cbConfig);
  }

  public YBATask restoreContinuousBackup(
      Http.Request request, UUID cUUID, ContinuousRestoreSpec spec) throws Exception {
    Customer customer = Customer.getOrBadRequest(cUUID);
    RestoreContinuousBackup.Params taskParams = new RestoreContinuousBackup.Params();
    taskParams.storageConfigUUID = spec.getStorageConfigUuid();
    taskParams.backupDir = spec.getBackupDir();
    UUID taskUUID = ybaBackupHandler.restoreContinuousBackup(customer, taskParams);
    return new YBATask().taskUuid(taskUUID);
  }
}
