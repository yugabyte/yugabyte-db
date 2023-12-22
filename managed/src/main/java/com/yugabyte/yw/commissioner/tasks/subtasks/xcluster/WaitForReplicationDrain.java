package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.google.common.base.Stopwatch;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.WaitForReplicationDrainResponse;
import org.yb.client.YBClient;

@Slf4j
public class WaitForReplicationDrain extends XClusterConfigTaskBase {

  @Inject
  protected WaitForReplicationDrain(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
  }

  public static class Params extends XClusterConfigTaskParams {
    // The source universe UUID must be stored in universeUUID field.
    // The parent xCluster config must be stored in xClusterConfig field.
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s(xClusterConfig=%s)", super.getName(), taskParams().getXClusterConfig());
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();

    if (!Objects.equals(taskParams().getUniverseUUID(), xClusterConfig.getSourceUniverseUUID())) {
      throw new IllegalArgumentException(
          String.format(
              "WaitForReplicationDrain must be run against the source universe; "
                  + "source universe uuid is %s and the universe uuid in the taskparams is %s",
              xClusterConfig.getSourceUniverseUUID(), taskParams().getUniverseUUID()));
    }

    if (!xClusterConfig.getType().equals(ConfigType.Txn)) {
      throw new IllegalArgumentException(
          String.format(
              "WaitForReplicationDrain only works for Txn xCluster; the current type is %s",
              xClusterConfig.getType()));
    }

    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    Duration subtaskTimeout =
        this.confGetter.getConfForScope(universe, UniverseConfKeys.waitForReplicationDrainTimeout);
    String universeMasterAddresses = universe.getMasterAddresses();
    String universeCertificate = universe.getCertificateNodetoNode();
    try (YBClient client = ybService.getClient(universeMasterAddresses, universeCertificate)) {
      List<String> activeStreamIds =
          new ArrayList<>(xClusterConfig.getStreamIdsWithReplicationSetup());
      Stopwatch stopwatch = Stopwatch.createStarted();
      Duration subtaskElapsedTime;
      int iterationNumber = 0;
      // Loop until there is no undrained replication streams.
      while (true) {
        log.info("Running waitForReplicationDrain for streams {}", activeStreamIds);
        WaitForReplicationDrainResponse resp = client.waitForReplicationDrain(activeStreamIds);
        if (resp.hasError()) {
          throw new RuntimeException(
              String.format(
                  "waitForReplicationDrain failed universe %s on XClusterConfig(%s): %s",
                  universe.getUniverseUUID(), xClusterConfig, resp.errorMessage()));
        }
        List<String> undrainedStreamIds =
            resp.getUndrainedStreams().stream()
                .map(streamInfo -> streamInfo.getStreamId().toStringUtf8())
                .collect(Collectors.toList());
        if (undrainedStreamIds.isEmpty()) {
          log.info("All streams are drained");
          break;
        }
        subtaskElapsedTime = stopwatch.elapsed();
        if (subtaskElapsedTime.compareTo(subtaskTimeout) > 0) {
          log.warn("Streams {} are not drained", undrainedStreamIds);
        } else {
          log.warn("Streams {} are not drained; retrying...", undrainedStreamIds);
        }
        if (subtaskElapsedTime.compareTo(subtaskTimeout) > 0) {
          throw new RuntimeException(
              String.format(
                  "WaitForReplicationDrain: timing out after retrying %s times for a duration of "
                      + "%sms which is more than subtaskTimeout (%sms)",
                  iterationNumber, subtaskElapsedTime.toMillis(), subtaskTimeout.toMillis()));
        }
        iterationNumber++;
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }

    log.info("Completed {}", getName());
  }
}
