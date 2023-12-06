package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.metrics.MetricQueryResponse;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.time.Duration;
import java.util.ArrayList;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;

/** @deprecated - Use CheckFollowerLag */
@Deprecated
@Slf4j
public class WaitForFollowerLag extends AbstractTaskBase {

  // Time to wait (in millisec) during each iteration of follower lag check.
  private static final int WAIT_EACH_ATTEMPT_MS = 1000;

  // Log after these many iterations
  private static final int LOG_EVERY_NUM_ITERS = 10;

  public static final String MAX_FOLLOWER_LAG_THRESHOLD_MS =
      "yb.upgrade.max_follower_lag_threshold_ms";

  @Inject private MetricQueryHelper metricQueryHelper;

  @Inject
  protected WaitForFollowerLag(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends ServerSubTaskParams {
    public NodeDetails node;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    NodeDetails node = taskParams().node;
    ServerType serverType = taskParams().serverType;
    double epsilon = 0.00001d;
    String ip = null;
    int httpPort = 0;
    int maxFollowerLagThresholdMs =
        confGetter.getConfForScope(universe, UniverseConfKeys.ybUpgradeMaxFollowerLagThresholdMs);
    double followerLagMs = maxFollowerLagThresholdMs + 1;
    int numIters = 0;
    long startTimeMs = System.currentTimeMillis();
    long maxWaitTimeMs =
        confGetter.getConfForScope(universe, UniverseConfKeys.followerLagTimeout).toMillis();

    try {
      ip = Util.getNodeIp(universe, node);
      httpPort = node.tserverHttpPort;
      if (serverType == ServerType.MASTER) {
        httpPort = node.masterHttpPort;
      }
      followerLagMs = getFollowerLagMs(ip, httpPort);
      while ((followerLagMs - (double) maxFollowerLagThresholdMs) >= epsilon) {
        followerLagMs = getFollowerLagMs(ip, httpPort);
        waitFor(Duration.ofMillis(WAIT_EACH_ATTEMPT_MS));
        numIters++;
        if (numIters % LOG_EVERY_NUM_ITERS == 0) {
          log.info(
              "Info: iters={}, ip={}, port={}, followerLagMs={}",
              numIters,
              ip,
              httpPort,
              followerLagMs);
        }

        // if reached certain threshold of elapsed time, timeout and throw failed/abort the upgrade
        long curTimeMs = System.currentTimeMillis();
        long timeElapedMs = curTimeMs - startTimeMs;
        if (timeElapedMs >= maxWaitTimeMs) {
          throw new RuntimeException(
              String.format(
                  "Follower lag timeout reached: ip=%s, port=%d, followerLagMs=%f",
                  ip, httpPort, followerLagMs));
        }
      }

      log.debug("node {} ready after iters={}.", node.nodeName, numIters);
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }
  }

  private double getFollowerLagMs(String ip, int port) {
    String promQuery =
        String.format("max by (instance) (follower_lag_ms{instance='%s:%d'})", ip, port);
    try {
      // May throw if issue with prometheus, for now we treat as no follower lag
      ArrayList<MetricQueryResponse.Entry> response = metricQueryHelper.queryDirect(promQuery);
      if (response.size() != 0) {
        MetricQueryResponse.Entry entry = response.get(0);
        String instanceId = entry.labels.get("instance");
        if (!StringUtils.isEmpty(instanceId) && !CollectionUtils.isEmpty(entry.values)) {
          double followerLagMs = entry.values.get(0).getRight();
          return followerLagMs;
        }
      }
    } catch (RuntimeException e) {
      log.error(e.getMessage());
    }
    // cannot find follower lag ms or prometheus query failed, we assume no follower lag
    return 0;
  }
}
