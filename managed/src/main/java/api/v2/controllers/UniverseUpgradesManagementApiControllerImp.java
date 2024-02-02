package api.v2.controllers;

import api.v2.models.UniverseGFlags;
import api.v2.models.YBPTask;
import com.google.inject.Inject;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.controllers.UpgradeUniverseController;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Http;
import play.mvc.Result;

@Slf4j
public class UniverseUpgradesManagementApiControllerImp
    extends UniverseUpgradesManagementApiControllerImpInterface {
  @Inject private UpgradeUniverseController upgradeUniverseController;

  @Override
  public YBPTask upgradeGFlags(
      Http.Request request, UUID cUUID, UUID uniUUID, UniverseGFlags universeGFlags)
      throws Exception {
    log.info("Starting upgrade GFlags with {}", universeGFlags);

    // get universe from db
    Customer customer = Customer.getOrBadRequest(cUUID);
    Universe universe = Universe.getOrBadRequest(uniUUID, customer);
    // fill in SpecificGFlags from universeGFlags params into universe
    UniverseDefinitionTaskParams taskParams = universe.getUniverseDetails();
    if (universeGFlags.getPrimaryGflags() != null
        && universeGFlags.getPrimaryGflags().getPerProcessGflags() != null) {
      Map<String, String> newMasterGFlags =
          universeGFlags.getPrimaryGflags().getPerProcessGflags().getMasterGflags();
      Map<String, String> newTServerGFlags =
          universeGFlags.getPrimaryGflags().getPerProcessGflags().getTserverGflags();
      taskParams.getPrimaryCluster().userIntent.specificGFlags =
          SpecificGFlags.construct(newMasterGFlags, newTServerGFlags);
    }
    if (universeGFlags.getReadReplicaGflags() != null
        && universeGFlags.getReadReplicaGflags().getPerProcessGflags() != null) {
      Map<String, String> newMasterGFlags =
          universeGFlags.getReadReplicaGflags().getPerProcessGflags().getMasterGflags();
      Map<String, String> newTServerGFlags =
          universeGFlags.getReadReplicaGflags().getPerProcessGflags().getTserverGflags();
      if (!taskParams.getNonPrimaryClusters().isEmpty()) {
        taskParams.getNonPrimaryClusters().get(0).userIntent.specificGFlags =
            SpecificGFlags.construct(newMasterGFlags, newTServerGFlags);
      }
    }
    // construct a GFlagsUpgradeParams that includes above universe details and
    // invoke v1 upgrade api UpgradeUniverseController.upgradeGFlags
    request =
        convertToRequest(universe.getUniverseDetails(), GFlagsUpgradeParams.class, null, request);
    Result result = upgradeUniverseController.upgradeGFlags(cUUID, uniUUID, request);
    // extract the v1 Task from the result, and construct a v2 Task to return from here
    PlatformResults.YBPTask v1Task = extractFromResult(result, PlatformResults.YBPTask.class);
    YBPTask ybpTask = new YBPTask().taskUuid(v1Task.taskUUID).resourceUuid(v1Task.resourceUUID);

    log.info("Started gflags upgrade task {}", mapper.writeValueAsString(ybpTask));
    return ybpTask;
  }
}
