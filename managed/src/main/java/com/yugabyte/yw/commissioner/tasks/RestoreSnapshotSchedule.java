package com.yugabyte.yw.commissioner.tasks;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.RestoreSnapshotScheduleParams;
import com.yugabyte.yw.models.Universe;
import java.time.Duration;
import java.util.List;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.ListSnapshotRestorationsResponse;
import org.yb.client.RestoreSnapshotScheduleResponse;
import org.yb.client.SnapshotRestorationInfo;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo.SysSnapshotEntryPB.State;

@Slf4j
public class RestoreSnapshotSchedule extends UniverseTaskBase {
  public static final List<State> RESTORATION_VALID_STATES =
      ImmutableList.of(State.RESTORING, State.RESTORED);

  @Inject
  protected RestoreSnapshotSchedule(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected RestoreSnapshotScheduleParams taskParams() {
    return (RestoreSnapshotScheduleParams) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s(universeUuid=%s, pitrConfigUUID=%s, restoreTimeInMillis=%s)",
        super.getName(),
        taskParams().getUniverseUUID(),
        taskParams().pitrConfigUUID,
        taskParams().restoreTimeInMillis);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    try (YBClient client = ybService.getUniverseClient(universe)) {

      UUID restorationUuid = null;

      // First, check if a restoration matching the schedule ID and restore time already exists.
      ListSnapshotRestorationsResponse restorationsResponse =
          client.listSnapshotRestorations(null /* restorationUUID */);

      List<SnapshotRestorationInfo> restorations =
          restorationsResponse.getSnapshotRestorationInfoList();
      for (SnapshotRestorationInfo restoration : restorations) {
        // Find restoration matching schedule UUID and restore time.
        if (restoration.getScheduleUUID().equals(taskParams().pitrConfigUUID)) {
          long restoreAtMillis = restoration.getRestoreTime();
          if (restoreAtMillis == taskParams().restoreTimeInMillis) {
            restorationUuid = restoration.getRestorationUUID();
            break;
          }
        }
      }

      if (restorationUuid == null) {
        // Start a new restoration since no matching restoration was found.
        RestoreSnapshotScheduleResponse resp =
            client.restoreSnapshotSchedule(
                taskParams().pitrConfigUUID, taskParams().restoreTimeInMillis);
        if (resp.hasError()) {
          String errorMsg = getName() + " failed due to error: " + resp.errorMessage();
          log.error(errorMsg);
          throw new RuntimeException(errorMsg);
        }
        restorationUuid = resp.getRestorationUUID();
        log.info(
            "Restoration matching schedule UUID {} and restore time {} created. "
                + "Restoration UUID: {}",
            taskParams().pitrConfigUUID,
            taskParams().restoreTimeInMillis,
            restorationUuid);
      } else {
        log.info(
            "Restoration matching schedule UUID {} and restore time {} already exists. "
                + "Restoration UUID: {}",
            taskParams().pitrConfigUUID,
            taskParams().restoreTimeInMillis,
            restorationUuid);
      }

      // Ensure that the restoration reaches the RESTORED state.
      ensureStateIsRestored(client, universe, restorationUuid);
    } catch (Exception e) {
      log.error("{} hit exception: {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }

    log.info("Completed {}", getName());
  }

  private void ensureStateIsRestored(YBClient client, Universe universe, UUID restorationUuid) {
    Duration pitrRestorePollDelay =
        confGetter.getConfForScope(universe, UniverseConfKeys.pitrRestorePollDelay);
    long pitrRestorePollDelayMs = pitrRestorePollDelay.toMillis();
    long pitrRestoreTimeoutMs =
        confGetter.getConfForScope(universe, UniverseConfKeys.pitrRestoreTimeout).toMillis();
    long startTime = System.currentTimeMillis();
    long remainingTimeoutMs = pitrRestoreTimeoutMs - (System.currentTimeMillis() - startTime);

    doWithConstTimeout(
        pitrRestorePollDelayMs,
        remainingTimeoutMs,
        () -> {
          try {
            ListSnapshotRestorationsResponse listSnapshotRestorationsResponse =
                client.listSnapshotRestorations(restorationUuid);

            List<SnapshotRestorationInfo> snapshotRestorationInfoList =
                listSnapshotRestorationsResponse.getSnapshotRestorationInfoList();
            SnapshotRestorationInfo snapshotRestorationInfo =
                snapshotRestorationInfoList.stream()
                    .filter(sri -> restorationUuid.equals(sri.getRestorationUUID()))
                    .findFirst()
                    .orElseThrow(
                        () ->
                            new RuntimeException(
                                String.format(
                                    "Restore snapshot response for restore UUID %s not found",
                                    restorationUuid)));

            State currentState = snapshotRestorationInfo.getState();
            if (State.RESTORED.equals(currentState)) {
              // Restoration is complete.
              return;
            }
            if (State.RESTORING.equals(currentState)) {
              // Restoration is still in progress; continue polling.
              throw new RuntimeException("Snapshot is still in RESTORING state");
            }
            throw new RuntimeException(
                String.format(
                    "Restoration is in an invalid state: %s. Valid states are: %s",
                    currentState, RESTORATION_VALID_STATES));
          } catch (Exception e) {
            throw new RuntimeException(e);
          }
        });
  }
}
