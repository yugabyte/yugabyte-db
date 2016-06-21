// Copyright (c) YugaByte, Inc.

package services;

import com.google.inject.Inject;
import java.util.concurrent.CompletableFuture;
import javax.inject.Singleton;
import play.inject.ApplicationLifecycle;
import util.Util;

import org.yb.client.MiniYBCluster;

@Singleton
public class LocalYBMiniClusterService implements YBMiniClusterService {
  // For starters hardcode the value, we will be called with required masters as needed.
  int numMasters = 3;
  private MiniYBCluster miniCluster = null;

  @Inject
  public LocalYBMiniClusterService(ApplicationLifecycle lifecycle) {
    if (!Util.isLocalTesting())
      return;

    miniCluster = Util.getMiniCluster(numMasters);

    lifecycle.addStopHook(() -> {
      // TODO: Close causes a org/apache/commons/io/FileUtils NoClassDefFoundError
      Util.closeMiniCluster(miniCluster);
      return CompletableFuture.completedFuture(null);
    });
  }

  @Override
  public synchronized MiniYBCluster getMiniCluster(int nMasters) {
    // Need to specify local testing env parameter, to get a non-null miniCluster.
    if (miniCluster != null && nMasters != 0 && numMasters != nMasters) {
      Util.closeMiniCluster(miniCluster);
      numMasters = nMasters;
      miniCluster = Util.getMiniCluster(numMasters);
    }
    return miniCluster;
  }
}
