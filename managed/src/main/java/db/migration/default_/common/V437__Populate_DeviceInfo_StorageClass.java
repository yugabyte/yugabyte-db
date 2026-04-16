// Copyright (c) YugaByte, Inc.

package db.migration.default_.common;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.helm.HelmUtils;
import com.yugabyte.yw.common.yaml.SkipNullRepresenter;
import com.yugabyte.yw.models.migrations.V437.AZOverrides;
import com.yugabyte.yw.models.migrations.V437.AvailabilityZone;
import com.yugabyte.yw.models.migrations.V437.CloudInfoInterfaceHelper;
import com.yugabyte.yw.models.migrations.V437.DeviceInfo;
import com.yugabyte.yw.models.migrations.V437.PerProcessDetails;
import com.yugabyte.yw.models.migrations.V437.Provider;
import com.yugabyte.yw.models.migrations.V437.Region;
import com.yugabyte.yw.models.migrations.V437.Universe;
import io.ebean.DB;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;
import org.yaml.snakeyaml.Yaml;
import play.libs.Json;

@Slf4j
public class V437__Populate_DeviceInfo_StorageClass extends BaseJavaMigration {

  @Override
  public void migrate(Context context) {
    DB.execute(V437__Populate_DeviceInfo_StorageClass::migrateDeviceInfoStorageClass);
  }

  public static void migrateDeviceInfoStorageClass() {
    ObjectMapper objectMapper = new ObjectMapper();
    for (Universe universe : Universe.getAll()) {
      if (universe.getConfig().containsKey(com.yugabyte.yw.models.Universe.VOLUME_MIGRATED)) {
        continue;
      }
      try {
        JsonNode universeDetailsJson = Json.parse(universe.universeDetailsJson);
        boolean updated = processUniverse(universeDetailsJson, universe.universeUUID);
        if (updated) {
          String updatedJsonString =
              objectMapper.writerWithDefaultPrettyPrinter().writeValueAsString(universeDetailsJson);
          universe.setUniverseDetailsJson(updatedJsonString);
          log.info("Updated deviceInfo storageClass for universe {}", universe.universeUUID);
        }
        universe.getConfig().put(com.yugabyte.yw.models.Universe.VOLUME_MIGRATED, "true");
        universe.save();
      } catch (Exception e) {
        log.error("Error processing universe {}: {}", universe.universeUUID, e.getMessage(), e);
      }
    }
  }

  private static boolean processUniverse(JsonNode universeDetailsJson, UUID universeUuid) {
    if (universeDetailsJson == null || !universeDetailsJson.has("clusters")) {
      return false;
    }

    JsonNode clustersNode = universeDetailsJson.get("clusters");
    if (!clustersNode.isArray()) {
      return false;
    }

    boolean updated = false;
    ArrayNode clustersArray = (ArrayNode) clustersNode;

    // Extract universe level overrides string and azOverrides string from primary
    // cluster intent
    String universeOverridesStr = null;
    Map<String, String> azsOverrides = new HashMap<>();
    for (JsonNode clusterNode : clustersArray) {
      if (clusterNode.get("clusterType").asText().equals("PRIMARY")) {
        if (!clusterNode.has("userIntent")) {
          continue;
        }
        JsonNode userIntentNode = clusterNode.get("userIntent");
        if (userIntentNode.has("universeOverrides")
            && !userIntentNode.get("universeOverrides").isNull()) {
          universeOverridesStr = userIntentNode.get("universeOverrides").asText();
        }
        if (userIntentNode.has("azOverrides") && !userIntentNode.get("azOverrides").isNull()) {
          ObjectMapper objectMapper = new ObjectMapper();
          azsOverrides = objectMapper.convertValue(userIntentNode.get("azOverrides"), Map.class);
        }
      }
    }
    // Get storage class from universeOverrides
    DeviceInfo universeTserverDeviceInfo = null, universeMasterDeviceInfo = null;
    if (StringUtils.isNotBlank(universeOverridesStr)) {
      Map<String, Object> universeOverridesMap = HelmUtils.convertYamlToMap(universeOverridesStr);
      universeTserverDeviceInfo = extractStorageProperty(universeOverridesMap, "tserver");
      universeMasterDeviceInfo = extractStorageProperty(universeOverridesMap, "master");
    }

    for (JsonNode clusterNode : clustersArray) {
      if (!clusterNode.has("userIntent")) {
        continue;
      }
      JsonNode userIntentNode = clusterNode.get("userIntent");

      Yaml yaml = new Yaml(new SkipNullRepresenter());
      ObjectMapper mapper = new ObjectMapper();

      int baseTsScOverrideLevel = 1, baseMasterScOverrideLevel = 1;

      Provider provider = getProviderFromUserIntent(userIntentNode, universeUuid);
      Map<String, String> config = CloudInfoInterfaceHelper.fetchEnvVars(provider);
      String storageClassProv = config != null ? config.get("STORAGE_CLASS") : null;
      String overridesProv = config != null ? config.get("OVERRIDES") : null;

      DeviceInfo tserverBaseDeviceInfo = DeviceInfo.defaultTserverDeviceInfo(),
          masterBaseDeviceInfo = DeviceInfo.defaultMasterDeviceInfo();

      // Case 1(base user intent): Extract from userIntent DeviceInfo
      DeviceInfo userIntentTsDeviceInfo = getDeviceInfoFromUserIntent(userIntentNode, "tserver");
      if (userIntentTsDeviceInfo != null) {
        tserverBaseDeviceInfo.mergeDeviceInfo(userIntentTsDeviceInfo);
      }
      DeviceInfo userIntentMasterDeviceInfo = getDeviceInfoFromUserIntent(userIntentNode, "master");
      if (userIntentMasterDeviceInfo != null) {
        masterBaseDeviceInfo.mergeDeviceInfo(userIntentMasterDeviceInfo);
      }

      // Case 2(base user intent): Extract from Provider storage class
      if (StringUtils.isNotBlank(storageClassProv)) {
        baseTsScOverrideLevel = 2;
        baseMasterScOverrideLevel = 2;
        tserverBaseDeviceInfo.storageClass = storageClassProv;
        masterBaseDeviceInfo.storageClass = storageClassProv;
      }

      // Case 3(base user intent): Extract from Provider overrides
      if (StringUtils.isNotBlank(overridesProv)) {
        Map<String, Object> overridesProvMap = yaml.load(overridesProv);
        DeviceInfo tsDeviceInfo = extractStorageProperty(overridesProvMap, "tserver");
        if (tsDeviceInfo != null) {
          baseTsScOverrideLevel = 3;
          tserverBaseDeviceInfo.mergeDeviceInfo(tsDeviceInfo);
        }
        DeviceInfo masterDeviceInfo = extractStorageProperty(overridesProvMap, "master");
        if (masterDeviceInfo != null) {
          baseMasterScOverrideLevel = 3;
          masterBaseDeviceInfo.mergeDeviceInfo(masterDeviceInfo);
        }
      }

      // Case 4(base user intent): Extract from Universe overrides
      if (universeTserverDeviceInfo != null) {
        baseTsScOverrideLevel = 4;
        tserverBaseDeviceInfo.mergeDeviceInfo(universeTserverDeviceInfo);
      }
      if (universeMasterDeviceInfo != null) {
        baseMasterScOverrideLevel = 4;
        masterBaseDeviceInfo.mergeDeviceInfo(universeMasterDeviceInfo);
      }

      if (!clusterNode.has("placementInfo")) {
        continue;
      }
      JsonNode placementInfoNode = clusterNode.get("placementInfo");
      if (!placementInfoNode.has("cloudList")) {
        continue;
      }
      JsonNode cloudListNode = placementInfoNode.get("cloudList");
      if (!cloudListNode.isArray() || cloudListNode.size() == 0) {
        continue;
      }
      JsonNode cloudData = cloudListNode.get(0);
      if (!cloudData.has("regionList")) {
        continue;
      }
      JsonNode regionListNode = cloudData.get("regionList");
      if (!regionListNode.isArray()) {
        continue;
      }
      for (JsonNode regionNode : regionListNode) {
        if (!regionNode.has("uuid")) {
          continue;
        }
        UUID regionUuid;
        try {
          regionUuid = UUID.fromString(regionNode.get("uuid").asText());
        } catch (IllegalArgumentException e) {
          continue;
        }
        Region region = Region.getOrBadRequest(regionUuid);
        // Region config
        Map<String, String> regionConfig = CloudInfoInterfaceHelper.fetchEnvVars(region);

        if (!regionNode.has("azList")) {
          continue;
        }
        JsonNode azListNode = regionNode.get("azList");
        if (!azListNode.isArray()) {
          continue;
        }
        for (JsonNode azNode : azListNode) {
          if (!azNode.has("uuid")) {
            continue;
          }
          UUID azUuid;
          try {
            azUuid = UUID.fromString(azNode.get("uuid").asText());
          } catch (IllegalArgumentException e) {
            continue;
          }
          AvailabilityZone az = AvailabilityZone.getOrBadRequest(azUuid);
          // AZ config
          Map<String, String> azConfig = CloudInfoInterfaceHelper.fetchEnvVars(az);

          DeviceInfo tserverAZOverridenDeviceInfo = new DeviceInfo(),
              masterAZOverridenDeviceInfo = new DeviceInfo();

          // Case 1(user intent overrides): Extract from Provider STORAGE_CLASS at region/az
          // Priority: az > region
          String storageClassProvAZ = azConfig != null ? azConfig.get("STORAGE_CLASS") : null;
          String storageClassProvRegion =
              regionConfig != null ? regionConfig.get("STORAGE_CLASS") : null;

          if (StringUtils.isNotBlank(storageClassProvAZ)
              || StringUtils.isNotBlank(storageClassProvRegion)) {
            String sc =
                StringUtils.isNotBlank(storageClassProvAZ)
                    ? storageClassProvAZ
                    : storageClassProvRegion;
            tserverAZOverridenDeviceInfo.storageClass = sc;
            masterAZOverridenDeviceInfo.storageClass = sc;
            if (baseTsScOverrideLevel > 2) {
              tserverAZOverridenDeviceInfo.unsetFields(tserverBaseDeviceInfo);
            }
            if (baseMasterScOverrideLevel > 2) {
              masterAZOverridenDeviceInfo.unsetFields(masterBaseDeviceInfo);
            }
          }

          // Case 2(user intent overrides): Extract from provider OVERRIDES at region/az
          // Priority: az > region
          String overridesProvAZ = azConfig != null ? azConfig.get("OVERRIDES") : null;
          String overridesProvRegion = regionConfig != null ? regionConfig.get("OVERRIDES") : null;

          if (StringUtils.isNotBlank(overridesProvAZ)) {
            Map<String, Object> overridesProvAZMap = yaml.load(overridesProvAZ);
            DeviceInfo tsDeviceInfo = extractStorageProperty(overridesProvAZMap, "tserver");
            if (tsDeviceInfo != null) {
              tserverAZOverridenDeviceInfo.mergeDeviceInfo(tsDeviceInfo);
              if (baseTsScOverrideLevel > 3) {
                tserverAZOverridenDeviceInfo.unsetFields(tserverBaseDeviceInfo);
              }
            }
            DeviceInfo masterDeviceInfo = extractStorageProperty(overridesProvAZMap, "master");
            if (masterDeviceInfo != null) {
              masterAZOverridenDeviceInfo.mergeDeviceInfo(masterDeviceInfo);
              if (baseMasterScOverrideLevel > 3) {
                masterAZOverridenDeviceInfo.unsetFields(masterDeviceInfo);
              }
            }
          } else if (StringUtils.isNotBlank(overridesProvRegion)) {
            Map<String, Object> overridesProvRegionMap = yaml.load(overridesProvRegion);
            DeviceInfo tsDeviceInfo = extractStorageProperty(overridesProvRegionMap, "tserver");
            if (tsDeviceInfo != null) {
              tserverAZOverridenDeviceInfo.mergeDeviceInfo(tsDeviceInfo);
              if (baseTsScOverrideLevel > 3) {
                tserverAZOverridenDeviceInfo.unsetFields(tserverBaseDeviceInfo);
              }
            }
            DeviceInfo masterDeviceInfo = extractStorageProperty(overridesProvRegionMap, "master");
            if (masterDeviceInfo != null) {
              masterAZOverridenDeviceInfo.mergeDeviceInfo(masterDeviceInfo);
              if (baseMasterScOverrideLevel > 3) {
                masterAZOverridenDeviceInfo.unsetFields(masterDeviceInfo);
              }
            }
          }

          // Case 3(user intent overrides): Extract from azOverrides
          String azOverridesStr = azsOverrides.get(az.getCode());
          if (StringUtils.isNotBlank(azOverridesStr)) {
            Map<String, Object> azOverridesMap = HelmUtils.convertYamlToMap(azOverridesStr);
            DeviceInfo tsDeviceInfo = extractStorageProperty(azOverridesMap, "tserver");
            if (tsDeviceInfo != null) {
              tserverAZOverridenDeviceInfo.mergeDeviceInfo(tsDeviceInfo);
              if (baseTsScOverrideLevel > 4) {
                tserverAZOverridenDeviceInfo.unsetFields(tserverBaseDeviceInfo);
              }
            }
            DeviceInfo masterDeviceInfo = extractStorageProperty(azOverridesMap, "master");
            if (masterDeviceInfo != null) {
              masterAZOverridenDeviceInfo.mergeDeviceInfo(masterDeviceInfo);
              if (baseMasterScOverrideLevel > 4) {
                masterAZOverridenDeviceInfo.unsetFields(masterDeviceInfo);
              }
            }
          }

          if ((tserverAZOverridenDeviceInfo != null && !tserverAZOverridenDeviceInfo.allNull())
              || (masterAZOverridenDeviceInfo != null && !masterAZOverridenDeviceInfo.allNull())) {
            if (!userIntentNode.has("userIntentOverrides")) {
              ((ObjectNode) userIntentNode).set("userIntentOverrides", mapper.createObjectNode());
            }
            ObjectNode userIntentOverridesNode =
                (ObjectNode) userIntentNode.get("userIntentOverrides");
            if (!userIntentOverridesNode.has("azOverrides")) {
              userIntentOverridesNode.set("azOverrides", mapper.createObjectNode());
            }
            ObjectNode azOverridesUserIntentOverrides =
                (ObjectNode) userIntentOverridesNode.get("azOverrides");
            Map<String, Object> azOMap =
                mapper.convertValue(
                    azOverridesUserIntentOverrides, new TypeReference<Map<String, Object>>() {});
            Map<String, PerProcessDetails> perProcessMap = new HashMap<>();

            if (masterAZOverridenDeviceInfo != null && !masterAZOverridenDeviceInfo.allNull()) {
              PerProcessDetails perProcMaster = new PerProcessDetails(masterAZOverridenDeviceInfo);
              perProcessMap.put("MASTER", perProcMaster);
            }
            if (tserverAZOverridenDeviceInfo != null && !tserverAZOverridenDeviceInfo.allNull()) {
              PerProcessDetails perProcTserver =
                  new PerProcessDetails(tserverAZOverridenDeviceInfo);
              perProcessMap.put("TSERVER", perProcTserver);
            }
            AZOverrides azOverridesMap = new AZOverrides(perProcessMap);
            azOMap.put(az.uuid.toString(), azOverridesMap);
            userIntentOverridesNode.set("azOverrides", mapper.valueToTree(azOMap));
            updated = true;
          }
        }
        // Write values to userIntent
        // Apply base deviceInfo and masterDeviceInfo
        if (tserverBaseDeviceInfo != null) {
          ((ObjectNode) userIntentNode)
              .replace("deviceInfo", mapper.valueToTree(tserverBaseDeviceInfo));
          updated = true;
        }
        if (masterBaseDeviceInfo != null) {
          ((ObjectNode) userIntentNode)
              .replace("masterDeviceInfo", mapper.valueToTree(masterBaseDeviceInfo));
          updated = true;
        }
      }
    }
    return updated;
  }

  private static Provider getProviderFromUserIntent(JsonNode userIntentNode, UUID universeUUID) {
    if (!userIntentNode.has("providerType")) {
      return null;
    }
    String providerType = userIntentNode.get("providerType").asText();
    if (!"kubernetes".equals(providerType)) {
      return null;
    }

    if (!userIntentNode.has("provider")) {
      return null;
    }

    String providerUuidStr = userIntentNode.get("provider").asText();
    UUID providerUuid;
    try {
      providerUuid = UUID.fromString(providerUuidStr);
    } catch (IllegalArgumentException e) {
      log.warn("Invalid provider UUID {} for universe {}", providerUuidStr, universeUUID);
      return null;
    }

    return Provider.getOrBadRequest(providerUuid);
  }

  private static DeviceInfo getDeviceInfoFromUserIntent(
      JsonNode userIntentNode, String serverType) {
    String property = serverType.equals("tserver") ? "deviceInfo" : "masterDeviceInfo";
    if (!userIntentNode.has(property)) {
      return null;
    }
    JsonNode deviceInfoNode = userIntentNode.get(property);
    ObjectMapper mapper = new ObjectMapper();
    try {
      return mapper.treeToValue(deviceInfoNode, DeviceInfo.class);
    } catch (JsonProcessingException | IllegalArgumentException e) {
      log.error("Error reading deviceInfo from userIntent");
      return null;
    }
  }

  @SuppressWarnings("unchecked")
  private static DeviceInfo extractStorageProperty(
      Map<String, Object> overridesMap, String serverType) {
    if (overridesMap == null || overridesMap.isEmpty()) {
      return null;
    }

    Object storageObj = overridesMap.get("storage");
    if (storageObj == null || !(storageObj instanceof Map)) {
      return null;
    }

    Map<String, Object> storageMap = (Map<String, Object>) storageObj;
    Object componentObj = storageMap.get(serverType);
    if (componentObj == null || !(componentObj instanceof Map)) {
      return null;
    }
    DeviceInfo deviceInfo = new DeviceInfo();

    Map<String, Object> componentMap = (Map<String, Object>) componentObj;
    // storageClass
    Object storageClassObj = componentMap.get("storageClass");
    if (storageClassObj != null) {
      deviceInfo.storageClass = storageClassObj.toString();
    }
    // count
    Object countObj = componentMap.get("count");
    if (countObj != null) {
      deviceInfo.numVolumes = Integer.parseInt(countObj.toString());
    }
    // size
    Object sizeObj = componentMap.get("size");
    if (sizeObj != null) {
      String sizeStr = sizeObj.toString();
      // Remove "Gi" suffix if present
      if (sizeStr.endsWith("Gi")) {
        sizeStr = sizeStr.substring(0, sizeStr.length() - 2);
      }
      try {
        deviceInfo.volumeSize = Integer.parseInt(sizeStr.trim());
      } catch (NumberFormatException e) {
        log.warn("Could not parse volume size: {}", sizeStr);
      }
    }
    return deviceInfo;
  }
}
