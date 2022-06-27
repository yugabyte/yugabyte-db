package com.yugabyte.yw.common;

import static play.mvc.Http.Status.BAD_REQUEST;

import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.stream.Collectors;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.tasks.params.ScheduledAccessKeyRotateParams;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;

import io.ebean.annotation.Transactional;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
@Singleton
public class AccessKeyRotationUtil {

  @Inject AccessManager accessManager;

  public Set<UUID> getScheduledAccessKeyRotationUniverses(UUID customerUUID, UUID providerUUID) {
    Set<UUID> universeUUIDs = new HashSet<UUID>();
    // populate universeUUIDs with all universes in rotation for the given provider
    Schedule.getAllByCustomerUUIDAndType(customerUUID, TaskType.CreateAndRotateAccessKey)
        .stream()
        .filter(schedule -> (schedule.getOwnerUUID().equals(providerUUID)))
        .filter(schedule -> (schedule.getStatus().equals(Schedule.State.Active)))
        .forEach(
            schedule -> {
              ScheduledAccessKeyRotateParams taskParams =
                  Json.fromJson(schedule.getTaskParams(), ScheduledAccessKeyRotateParams.class);
              universeUUIDs.addAll(taskParams.getUniverseUUIDs());
            });
    return universeUUIDs;
  }

  public void failUniverseAlreadyInRotation(
      UUID customerUUID, UUID providerUUID, List<UUID> universeUUIDs) {
    Set<UUID> inRotation = getScheduledAccessKeyRotationUniverses(customerUUID, providerUUID);
    if (universeUUIDs.stream().anyMatch(universeUUID -> inRotation.contains(universeUUID))) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "One of the universe already in active scheduled rotation, "
              + "please stop that schedule and retry!");
    }
  }

  public void failManuallyProvisioned(UUID providerUUID, String newAccessKeyCode) {
    AccessKey providerAccessKey = AccessKey.getAll(providerUUID).get(0);
    if (providerAccessKey.getKeyInfo().skipProvisioning) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Provider has manually provisioned nodes, cannot rotate keys!");
    } else if (newAccessKeyCode != null
        && AccessKey.getOrBadRequest(providerUUID, newAccessKeyCode)
            .getKeyInfo()
            .skipProvisioning) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "New access key was made for manually provisoned nodes, please supply another key!");
    }
  }

  /**
   * Used to create a new key for a provider during scheduled access key rotation, @Transactional
   * annotation ensures that invalid keys are not left in the db
   *
   * @return new access key
   */
  @Transactional
  public AccessKey createAccessKeyForProvider(UUID customerUUID, UUID providerUUID) {
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    if (AccessKey.getAll(providerUUID).size() == 0) {
      throw new RuntimeException(
          "No access key exists for provider uuid: " + providerUUID.toString());
    }
    // use latest created key to fill params
    AccessKey providerAccessKey = AccessKey.getLatestKey(providerUUID);
    AccessKey.KeyInfo keyInfo = providerAccessKey.getKeyInfo();
    String newKeyCode = AccessKey.getNewKeyCode(provider);
    AccessKey newAccessKey = null;
    // send a request to all regions, especially needed for AWS providers
    List<Region> regions = Region.getByProvider(providerUUID);
    for (Region region : regions) {
      newAccessKey =
          accessManager.addKey(
              region.uuid,
              newKeyCode,
              null,
              keyInfo.sshUser,
              keyInfo.sshPort,
              keyInfo.airGapInstall,
              keyInfo.skipProvisioning,
              keyInfo.setUpChrony,
              keyInfo.ntpServers,
              keyInfo.showSetUpChrony);
    }

    if (newAccessKey == null) {
      throw new RuntimeException(
          "Failed to create a new access key for the provider: " + providerUUID.toString());
    }
    return newAccessKey;
  }

  public List<UUID> removeDeletedUniverses(List<UUID> universeUUIDs) {
    List<UUID> filteredUniverses =
        Universe.getAllWithoutResources(new HashSet<UUID>(universeUUIDs))
            .stream()
            .map(universe -> universe.universeUUID)
            .collect(Collectors.toList());
    return filteredUniverses;
  }

  public List<UUID> removePausedUniverses(List<UUID> universeUUIDs) {
    List<UUID> filteredUniverses =
        Universe.getAllWithoutResources(new HashSet<UUID>(universeUUIDs))
            .stream()
            .filter(universe -> !universe.getUniverseDetails().universePaused)
            .map(universe -> universe.universeUUID)
            .collect(Collectors.toList());
    return filteredUniverses;
  }

  public long convertDaysToMillis(int days) {
    return TimeUnit.DAYS.toMillis(days);
  }
}
