package com.yugabyte.yw.commissioner.tasks;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.RestoreSnapshotScheduleParams;
import com.yugabyte.yw.models.Universe;
import java.time.Duration;
import java.util.List;
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
    String masterAddresses = universe.getMasterAddresses();
    String universeCertificate = universe.getCertificateNodetoNode();
    try (YBClient client = ybService.getClient(masterAddresses, universeCertificate)) {
      RestoreSnapshotScheduleResponse resp =
          client.restoreSnapshotSchedule(
              taskParams().pitrConfigUUID, taskParams().restoreTimeInMillis);
      if (resp.hasError()) {
        String errorMsg = getName() + " failed due to error: " + resp.errorMessage();
        log.error(errorMsg);
        throw new RuntimeException(errorMsg);
      }

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
                  client.listSnapshotRestorations(resp.getRestorationUUID());
              List<SnapshotRestorationInfo> snapshotRestorationInfoList =
                  listSnapshotRestorationsResponse.getSnapshotRestorationInfoList();
              SnapshotRestorationInfo snapshotRestorationInfo =
                  snapshotRestorationInfoList.stream()
                      .filter(sri -> resp.getRestorationUUID().equals(sri.getRestorationUUID()))
                      .findFirst()
                      .orElseThrow(
                          () ->
                              new RuntimeException(
                                  String.format(
                                      "Restore snapshot response for restore uuid: %s not found",
                                      resp.getRestorationUUID())));

              if (State.RESTORED.equals(snapshotRestorationInfo.getState())) {
                return;
              }
              if (State.RESTORING.equals(snapshotRestorationInfo.getState())) {
                throw new RuntimeException("Snapshot is still in RESTORING state");
              }
              throw new RuntimeException(
                  String.format(
                      "Valid states for a restoration are %s, but the restoration is in an invalid"
                          + " state: %s",
                      RESTORATION_VALID_STATES, snapshotRestorationInfo.getState()));
            } catch (Exception e) {
              throw new RuntimeException(e);
            }
          });
    } catch (Exception e) {
      log.error("{} hit exception : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }

    log.info("Completed {}", getName());
  }

  private boolean needStateUpdateIdempotency(Object currentState, Object requestedState) {
    // This subtask is idempotent by nature, i.e., the rerun of the subtask with the same params
    // will end in the same state.
    return true;
  }
}
