// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import akka.actor.ActorSystem;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.ImmutableMap;
import com.google.common.net.HostAndPort;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.Arrays;
import java.util.Base64;
import java.util.List;
import java.util.Map;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.stream.Collectors;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.YBClient;
import play.api.Play;
import scala.concurrent.ExecutionContext;
import scala.concurrent.duration.Duration;

@Singleton
public class SetUniverseKey {
    public static final Logger LOG = LoggerFactory.getLogger(SetUniverseKey.class);

    private AtomicBoolean running = new AtomicBoolean(false);

    private final ActorSystem actorSystem;

    private final ExecutionContext executionContext;

    private final EncryptionAtRestManager keyManager;

    private final YBClientService ybService;

    private final int YB_SET_UNIVERSE_KEY_INTERVAL = 2;

    @Inject
    public SetUniverseKey(
            EncryptionAtRestManager keyManager,
            ExecutionContext executionContext,
            ActorSystem actorSystem,
            YBClientService ybService
    ) {
        this.keyManager = keyManager;
        this.actorSystem = actorSystem;
        this.executionContext = executionContext;
        this.ybService = ybService;
        this.initialize();
    }

    private void initialize() {
        this.actorSystem.scheduler().schedule(
                Duration.create(0, TimeUnit.MINUTES),
                Duration.create(YB_SET_UNIVERSE_KEY_INTERVAL, TimeUnit.MINUTES),
                () -> scheduleRunner(),
                this.executionContext
        );
    }

    private void setKeyInMaster(Universe u, HostAndPort masterAddr, byte[] keyRef, byte[] keyVal) {
        YBClient client = null;
        String hostPorts = u.getMasterAddresses();
        String certificate = u.getCertificate();
        try {
            client = ybService.getClient(hostPorts, certificate);
            String encodedKeyRef = Base64.getEncoder().encodeToString(keyRef);
            if (!client.hasUniverseKeyInMemory(encodedKeyRef, masterAddr)) {
                client.addUniverseKeys(ImmutableMap.of(encodedKeyRef, keyVal), masterAddr);
            }
        } catch (Exception e) {
            String errMsg = String.format(
                    "Error sending universe encryption key to node %s",
                    masterAddr.toString()
            );
            LOG.error(errMsg, e);
        } finally {
            ybService.closeClient(client, hostPorts);
        }
    }

    private void setUniverseKey(Universe u) {
        Customer c = Customer.get(u.customerId);
        try {
            Map<String, String> encryptionAtRestConfig = u.getEncryptionAtRestConfig();
            boolean hasEncryptionMemory = keyManager.getNumKeyRotations(
                    c.uuid,
                    u.universeUUID,
                    encryptionAtRestConfig
            ) > 0;
            if (!u.universeIsLocked() && hasEncryptionMemory) {
                LOG.info(String.format(
                        "Setting universe encryption key for customer %s and universe %s",
                        c.uuid.toString(),
                        u.universeUUID.toString()
                ));
                byte[] keyRef = keyManager.getCurrentUniverseKeyRef(
                        c.uuid,
                        u.universeUUID,
                        encryptionAtRestConfig
                );
                byte[] keyVal = keyManager.getCurrentUniverseKey(
                        c.uuid,
                        u.universeUUID,
                        encryptionAtRestConfig,
                        keyRef
                );
                Arrays.stream(u.getMasterAddresses().split(","))
                        .map(addrString -> HostAndPort.fromString(addrString))
                        .forEach(addr -> setKeyInMaster(u, addr, keyRef, keyVal));
            }
        } catch (Exception e) {
            String errMsg = String.format(
                    "Error setting universe encryption key for customer %s and universe %s",
                    c.uuid.toString(),
                    u.universeUUID.toString()
            );
            LOG.error(errMsg, e);
        }
    }

    @VisibleForTesting
    void scheduleRunner() {
        if (running.get()) {
            LOG.info("Previous universe key setter still running");
            return;
        }

        LOG.info("Running universe key setter");
        running.set(true);
        try {
            Customer.getAll()
                    .stream()
                    .forEach(c -> c.getUniverses().stream().forEach(this::setUniverseKey));
        } catch (Exception e) {
            LOG.error("Error detected running universe key setter", e);
        }
        running.set(false);
    }
}
