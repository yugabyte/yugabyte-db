/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner;

import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.ImmutableMap;
import com.google.common.net.HostAndPort;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.KmsHistory;
import com.yugabyte.yw.models.Universe;
import java.time.Duration;
import java.util.Arrays;
import java.util.Base64;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.YBClient;

@Singleton
@Slf4j
public class SetUniverseKey {

  private final PlatformScheduler platformScheduler;

  private final EncryptionAtRestManager keyManager;

  private final YBClientService ybService;

  private static final int YB_SET_UNIVERSE_KEY_INTERVAL = 2;

  @Inject
  public SetUniverseKey(
      EncryptionAtRestManager keyManager,
      PlatformScheduler platformScheduler,
      YBClientService ybService) {
    this.keyManager = keyManager;
    this.platformScheduler = platformScheduler;
    this.ybService = ybService;
  }

  public void start() {
    platformScheduler.schedule(
        getClass().getSimpleName(),
        Duration.ZERO,
        Duration.ofMinutes(YB_SET_UNIVERSE_KEY_INTERVAL),
        this::scheduleRunner);
  }

  private void setKeyInMaster(Universe u, HostAndPort masterAddr, byte[] keyRef, byte[] keyVal) {

    YBClient client = null;

    // If the resume task is in progress the universe keys must be set for encryption to work.
    // Today on a paused universe, the only task which can run is Resume.
    if (u.getUniverseDetails().universePaused && !(u.getUniverseDetails().updateInProgress)) {
      log.info(
          "Skipping setting universe keys as {} is paused and no task is running",
          u.universeUUID.toString());
      return;
    }

    String hostPorts = u.getMasterAddresses();
    String certificate = u.getCertificateNodetoNode();
    try {
      client = ybService.getClient(hostPorts, certificate);
      String encodedKeyRef = Base64.getEncoder().encodeToString(keyRef);
      if (!client.hasUniverseKeyInMemory(encodedKeyRef, masterAddr)) {
        client.addUniverseKeys(ImmutableMap.of(encodedKeyRef, keyVal), masterAddr);
      }
    } catch (Exception e) {
      String errMsg =
          String.format("Error sending universe encryption key to node %s", masterAddr.toString());
      log.error(errMsg, e);
    } finally {
      ybService.closeClient(client, hostPorts);
    }
  }

  public void setUniverseKey(Universe u) {
    setUniverseKey(u, false /*force*/);
  }

  public void setUniverseKey(Universe u, boolean force) {
    try {
      if ((!u.universeIsLocked() || force)
          && EncryptionAtRestUtil.getNumKeyRotations(u.universeUUID) > 0) {
        log.debug(
            String.format(
                "Setting universe encryption key for universe %s", u.universeUUID.toString()));

        KmsHistory activeKey = EncryptionAtRestUtil.getActiveKey(u.universeUUID);
        if (activeKey == null
            || activeKey.uuid.keyRef == null
            || activeKey.uuid.keyRef.length() == 0) {
          final String errMsg =
              String.format("No active key found for universe %s", u.universeUUID.toString());
          log.debug(errMsg);
          return;
        }

        byte[] keyRef = Base64.getDecoder().decode(activeKey.uuid.keyRef);
        byte[] keyVal = keyManager.getUniverseKey(u.universeUUID, activeKey.configUuid, keyRef);
        Arrays.stream(u.getMasterAddresses().split(","))
            .map(HostAndPort::fromString)
            .forEach(addr -> setKeyInMaster(u, addr, keyRef, keyVal));
      }
    } catch (Exception e) {
      String errMsg =
          String.format(
              "Error setting universe encryption key for universe %s", u.universeUUID.toString());
      log.error(errMsg, e);
    }
  }

  public void handleCustomerError(UUID cUUID, Exception e) {
    log.error(
        String.format("Error detected running universe key setter for customer %s", cUUID), e);
  }

  public void setCustomerUniverseKeys(Customer c) {
    c.getUniverses().forEach(this::setUniverseKey);
  }

  @VisibleForTesting
  void scheduleRunner() {
    try {
      if (HighAvailabilityConfig.isFollower()) {
        log.debug("Skipping universe key setter for follower platform");
        return;
      }
      log.debug("Running universe key setter");
      Customer.getAll()
          .forEach(
              c -> {
                try {
                  setCustomerUniverseKeys(c);
                } catch (Exception e) {
                  handleCustomerError(c.uuid, e);
                }
              });
    } catch (Exception e) {
      log.error("Error running universe key setter", e);
    }
  }
}
