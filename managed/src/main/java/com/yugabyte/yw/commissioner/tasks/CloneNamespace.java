// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.backuprestore.BackupUtil.TABLE_TYPE_TO_YQL_DATABASE_MAP;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.CloneNamespaceParams;
import com.yugabyte.yw.models.Universe;
import java.time.Duration;
import java.util.List;
import java.util.Map;
import java.util.concurrent.atomic.AtomicReference;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.yb.client.CloneInfo;
import org.yb.client.CloneNamespaceResponse;
import org.yb.client.ListClonesResponse;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo.SysCloneStatePB.State;

@Slf4j
@Abortable
public class CloneNamespace extends UniverseTaskBase {
  public static final List<State> CLONE_VALID_STATES =
      ImmutableList.of(State.CREATING, State.COMPLETE);

  @Inject
  protected CloneNamespace(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected CloneNamespaceParams taskParams() {
    return (CloneNamespaceParams) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s(universeUuid=%s, customerUuid=%s, sourceKeyspaceName=%s, tableType=%s,"
            + " targetKeyspaceName=%s, cloneTimeInMillis=%s)",
        super.getName(),
        taskParams().getUniverseUUID(),
        taskParams().customerUUID,
        taskParams().keyspaceName,
        taskParams().tableType,
        taskParams().targetKeyspaceName,
        taskParams().cloneTimeInMillis);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    String masterAddresses = universe.getMasterAddresses();
    String universeCertificate = universe.getCertificateNodetoNode();
    try (YBClient client = ybService.getClient(masterAddresses, universeCertificate)) {
      // Find the keyspace id of the keyspace name specified in the task params.
      Map<String, String> keyspaceNameKeyspaceIdMap =
          getKeyspaceNameKeyspaceIdMap(client, taskParams().tableType);
      if (!keyspaceNameKeyspaceIdMap.containsKey(taskParams().keyspaceName)) {
        throw new IllegalArgumentException(
            String.format(
                "A keyspace with name %s and table type %s could not be found",
                taskParams().keyspaceName, taskParams().tableType));
      }
      String keyspaceId = keyspaceNameKeyspaceIdMap.get(taskParams().keyspaceName);
      log.debug(
          "Found keyspace id {} for source keyspace name {}",
          keyspaceId,
          taskParams().keyspaceName);

      // Target keyspace name should be unique.
      if (keyspaceNameKeyspaceIdMap.containsKey(taskParams().targetKeyspaceName)) {
        throw new IllegalArgumentException(
            String.format(
                "A keyspace with name %s and table type %s already exists",
                taskParams().targetKeyspaceName, taskParams().tableType));
      }

      // Clone the DB.
      CloneNamespaceResponse resp =
          client.cloneNamespace(
              TABLE_TYPE_TO_YQL_DATABASE_MAP.get(taskParams().tableType),
              taskParams().keyspaceName,
              keyspaceId,
              taskParams().targetKeyspaceName,
              taskParams().cloneTimeInMillis);
      if (resp.hasError()) {
        String errorMsg = getName() + " failed due to error: " + resp.errorMessage();
        log.error(errorMsg);
        throw new RuntimeException(errorMsg);
      }
      Integer cloneSeqNo = resp.getCloneSeqNo();
      if (cloneSeqNo == null) {
        throw new RuntimeException(
            String.format(
                "Clone request seq number was returned null for source keyspace: %s", keyspaceId));
      }

      Duration pitrClonePollDelay =
          confGetter.getConfForScope(universe, UniverseConfKeys.pitrClonePollDelay);
      long pitrClonePollDelayMs = pitrClonePollDelay.toMillis();
      long pitrCloneTimeoutMs =
          confGetter.getConfForScope(universe, UniverseConfKeys.pitrCloneTimeout).toMillis();
      long startTime = System.currentTimeMillis();

      // Get the clone info object.
      AtomicReference<CloneInfo> cloneInfoAtomicRef = new AtomicReference<>(null);
      doWithConstTimeout(
          pitrClonePollDelayMs,
          pitrCloneTimeoutMs,
          () -> {
            try {
              ListClonesResponse cloneResp = client.listClones(keyspaceId, cloneSeqNo);
              // cloneSeqNo is not null so the response list should contain only one valid clone
              List<CloneInfo> cloneInfoList = cloneResp.getCloneInfoList();
              CloneInfo clone = validateCloneInfo(keyspaceId, cloneSeqNo, cloneInfoList);
              cloneInfoAtomicRef.set(clone);
            } catch (Exception e) {
              throw new RuntimeException(e);
            }
          });

      // Poll to ensure the cloned DB is ready to use.
      long remainingTimeoutMs = pitrCloneTimeoutMs - (System.currentTimeMillis() - startTime);
      doWithConstTimeout(
          pitrClonePollDelayMs,
          remainingTimeoutMs,
          () -> {
            try {
              ListClonesResponse cloneResp = client.listClones(keyspaceId, cloneSeqNo);
              CloneInfo cloneInfo = cloneResp.getCloneInfoList().get(0);
              if (State.COMPLETE.equals(cloneInfo.getState())) {
                return;
              }
              if (State.CREATING.equals(cloneInfo.getState())) {
                throw new RuntimeException("Clone is still in CREATING state");
              }
              if (State.ABORTED.equals(cloneInfo.getState())) {
                throw new RuntimeException(
                    String.format(
                        "Clone creation has been aborted due to reason: %s",
                        cloneInfo.getAbortMessage()));
              }
              // Clone is in an intermittent state.
              throw new RuntimeException(
                  String.format("Clone is in an intermittent state: %s", cloneInfo.getState()));
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

  public static CloneInfo validateCloneInfo(
      String keyspaceId, Integer cloneSeqNo, List<CloneInfo> cloneInfoList) {
    if (CollectionUtils.size(cloneInfoList) != 1) {
      throw new RuntimeException(
          String.format(
              "More than one clones returned for source keyspace: %s and clone sequence number: %d",
              keyspaceId, cloneSeqNo));
    }
    CloneInfo clone = cloneInfoList.get(0);
    if (clone.getSourceNamespaceId().equals(keyspaceId)
        && clone.getCloneRequestSeqNo().equals(cloneSeqNo)) {
      return clone;
    }
    throw new RuntimeException(
        "Clone returned source namespace ID or clone sequence number are different than expected");
  }
}
