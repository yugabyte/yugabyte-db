// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.certmgmt.RpcClientCert;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import java.time.Duration;
import java.time.Instant;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

/**
 * Waits until YBA has a client cert for the universe's root CA: on create before the first
 * WaitForServer, and in a root CA rotation after Reset, so it fails before the roll that drops the
 * old CA if the issuer hasn't reissued yet.
 */
@Slf4j
public class WaitForRpcClientCert extends UniverseTaskBase {

  // cert-manager normally issues a cert in seconds.
  private static final Duration TIMEOUT = Duration.ofMinutes(5);
  private static final Duration POLL_INTERVAL = Duration.ofSeconds(5);

  private final RpcClientCert rpcClientCert;

  @Inject
  protected WaitForRpcClientCert(
      BaseTaskDependencies baseTaskDependencies, RpcClientCert rpcClientCert) {
    super(baseTaskDependencies);
    this.rpcClientCert = rpcClientCert;
  }

  public static class Params extends UniverseTaskParams {}

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    Universe universe = getUniverse();
    Instant deadline = Instant.now().plus(TIMEOUT);
    while (true) {
      try {
        rpcClientCert.load(universe);
        return;
      } catch (RuntimeException e) {
        if (Instant.now().isAfter(deadline)) {
          throw new RuntimeException(
              "Timed out waiting for a client cert for universe "
                  + universe.getName()
                  + ": "
                  + e.getMessage(),
              e);
        }
        log.info(
            "Waiting for a client cert for universe {}: {}", universe.getName(), e.getMessage());
      }
      waitFor(POLL_INTERVAL);
    }
  }
}
