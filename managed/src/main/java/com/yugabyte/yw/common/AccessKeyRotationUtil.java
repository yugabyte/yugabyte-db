package com.yugabyte.yw.common;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.tasks.params.ScheduledAccessKeyRotateParams;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AccessKeyId;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import io.ebean.annotation.Transactional;
import java.util.ArrayList;
import java.util.Date;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.function.Function;
import java.util.stream.Collectors;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.lang3.time.DateUtils;
import play.libs.Json;

@Singleton
public class AccessKeyRotationUtil {

  @Inject AccessManager accessManager;
  @Inject RuntimeConfGetter confGetter;

  public static final String SSH_KEY_EXPIRATION_ENABLED =
      "yb.security.ssh_keys.enable_ssh_key_expiration";
  public static final String SSH_KEY_EXPIRATION_THRESHOLD_DAYS =
      "yb.security.ssh_keys.ssh_key_expiration_threshold_days";

  public Set<UUID> getScheduledAccessKeyRotationUniverses(UUID customerUUID, UUID providerUUID) {
    Set<UUID> universeUUIDs = new HashSet<UUID>();
    // populate universeUUIDs with all universes in rotation for the given provider
    Schedule.getAllByCustomerUUIDAndType(customerUUID, TaskType.CreateAndRotateAccessKey).stream()
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
              region.getUuid(),
              newKeyCode,
              null,
              provider.getDetails().sshUser,
              provider.getDetails().sshPort,
              provider.getDetails().airGapInstall,
              provider.getDetails().skipProvisioning,
              provider.getDetails().setUpChrony,
              provider.getDetails().ntpServers,
              provider.getDetails().showSetUpChrony);
    }

    if (newAccessKey == null) {
      throw new RuntimeException(
          "Failed to create a new access key for the provider: " + providerUUID.toString());
    }
    return newAccessKey;
  }

  public List<UUID> removeDeletedUniverses(List<UUID> universeUUIDs) {
    List<UUID> filteredUniverses =
        Universe.getAllWithoutResources(new HashSet<UUID>(universeUUIDs)).stream()
            .map(universe -> universe.getUniverseUUID())
            .collect(Collectors.toList());
    return filteredUniverses;
  }

  public List<UUID> removePausedUniverses(List<UUID> universeUUIDs) {
    List<UUID> filteredUniverses =
        Universe.getAllWithoutResources(new HashSet<UUID>(universeUUIDs)).stream()
            .filter(universe -> !universe.getUniverseDetails().universePaused)
            .map(universe -> universe.getUniverseUUID())
            .collect(Collectors.toList());
    return filteredUniverses;
  }

  public long convertDaysToMillis(int days) {
    return TimeUnit.DAYS.toMillis(days);
  }

  // returns days to expiry of SSH key of a universe or
  // returns null if expiration is disabled and not set
  // calculates minimum over all clusters
  public Double getSSHKeyExpiryDays(Universe universe, Map<AccessKeyId, AccessKey> allAccessKeys) {

    boolean expirationEnabled =
        confGetter.getConfForScope(universe, UniverseConfKeys.enableSshKeyExpiration);
    int expirationThresholdDays =
        confGetter.getConfForScope(universe, UniverseConfKeys.sshKeyExpirationThresholdDays);

    List<AccessKey> universeAccessKeys = getUniverseAccessKeys(universe, allAccessKeys);
    // if universe is of a provider config such as K8s
    // where accessKeyCode is not set
    if (CollectionUtils.isEmpty(universeAccessKeys)) {
      return null;
    }
    // if expiration is disabled and no key has an explicitly expiration set
    if (!expirationEnabled
        && universeAccessKeys.stream()
            .allMatch(clusterAccessKey -> (clusterAccessKey.getExpirationDate() == null))) {
      return null;
    }

    long timeToExpiry = Long.MAX_VALUE;
    long currentTime = System.currentTimeMillis();
    for (AccessKey clusterAccessKey : universeAccessKeys) {
      if (clusterAccessKey.getExpirationDate() != null) {
        timeToExpiry =
            Math.min(timeToExpiry, clusterAccessKey.getExpirationDate().getTime() - currentTime);
      } else if (expirationEnabled) {
        Date expirationDate =
            DateUtils.addDays(clusterAccessKey.getCreationDate(), expirationThresholdDays);
        timeToExpiry = Math.min(timeToExpiry, expirationDate.getTime() - currentTime);
      }
    }
    return (double) TimeUnit.MILLISECONDS.toDays(timeToExpiry);
  }

  // ideally a universe should have the same access key for all clusters
  // still, we loop through each one as we store them at a cluster level
  public List<AccessKey> getUniverseAccessKeys(
      Universe universe, Map<AccessKeyId, AccessKey> allAccessKeys) {
    List<Cluster> clusters = universe.getUniverseDetails().clusters;
    List<AccessKey> accessKeys = new ArrayList<AccessKey>();
    clusters.forEach(
        cluster -> {
          String clusterAccessKeyCode = cluster.userIntent.accessKeyCode;
          if (StringUtils.isNotEmpty(clusterAccessKeyCode)) {
            UUID providerUUID = UUID.fromString(cluster.userIntent.provider);
            AccessKeyId id = AccessKeyId.create(providerUUID, clusterAccessKeyCode);
            accessKeys.add(allAccessKeys.get(id));
          }
        });
    return accessKeys;
  }

  public Map<AccessKeyId, AccessKey> createAllAccessKeysMap() {
    Map<AccessKeyId, AccessKey> accessKeys =
        AccessKey.getAll().stream()
            .collect(Collectors.toMap(accessKey -> accessKey.getIdKey(), Function.identity()));
    return accessKeys;
  }
}
