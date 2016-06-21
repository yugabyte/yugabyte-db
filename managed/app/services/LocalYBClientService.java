// Copyright (c) YugaByte, Inc.

package services;

import com.google.inject.Inject;
import org.yb.client.YBClient;
import javax.inject.Singleton;
import play.inject.ApplicationLifecycle;

import java.util.concurrent.CompletableFuture;

@Singleton
public class LocalYBClientService implements YBClientService {
  // For starters hardcode the value, we will be called with required hostports as needed.
  String masterHostPorts = "127.0.0.1:7101,127.0.0.1:7102,127.0.0.1:7103";
  private YBClient client = null;

  @Inject
  public LocalYBClientService(ApplicationLifecycle lifecycle) {
    client = getNewClient(masterHostPorts);

    lifecycle.addStopHook(() -> {
        client.close();
        return CompletableFuture.completedFuture(null);
    });
  }

  @Override
  public synchronized YBClient getClient(String masterHPs) {
    if (masterHPs != null && !masterHostPorts.equals(masterHPs)) {
      try {
        client.close();
      } catch (Exception e) {
        
      }
      client = getNewClient(masterHPs);

      masterHostPorts = masterHPs;
    }
    return client;
  }
  
  private YBClient getNewClient(String masterHPs) {
    return new YBClient.YBClientBuilder(masterHPs)
           .defaultAdminOperationTimeoutMs(600)
           .defaultOperationTimeoutMs(600)
           .build();
  }
}
