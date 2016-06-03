// Copyright (c) Yugabyte, Inc.

package services;

import com.google.inject.Inject;
import org.yb.client.YBClient;
import javax.inject.Singleton;
import play.inject.ApplicationLifecycle;

import java.util.concurrent.CompletableFuture;

@Singleton
public class LocalYBClientService implements YBClientService {
    // For now hardcode the values, we will eventually pull this from DB
    String masterPorts = "127.0.0.1:7101,127.0.0.1:7102,127.0.0.1:7103";
    private final YBClient client;

    @Inject
    public LocalYBClientService(ApplicationLifecycle lifecycle) {
        client = new YBClient.YBClientBuilder(masterPorts)
                .defaultAdminOperationTimeoutMs(600)
                .defaultOperationTimeoutMs(600)
                .build();

        lifecycle.addStopHook(() -> {
            client.close();
            return CompletableFuture.completedFuture(null);
        });
    }

    @Override
    public YBClient getClient() {
        return client;
    }
}
