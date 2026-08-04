// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.gflags;

import static com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType.MASTER;
import static com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType.TSERVER;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.gflags.GFlagGroup.GroupName;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.NoArgsConstructor;
import org.apache.commons.collections4.MapUtils;

@Data
@ApiModel(description = "GFlags for current cluster")
public class SpecificGFlags {

  @EqualsAndHashCode
  @AllArgsConstructor
  @NoArgsConstructor
  public static class PerProcessFlags {
    public Map<UniverseTaskBase.ServerType, Map<String, String>> value = new HashMap<>();

    @Override
    public String toString() {
      return value.toString();
    }
  }

  @ApiModelProperty private boolean inheritFromPrimary;

  @ApiModelProperty(value = "Gflags grouped by procces")
  private PerProcessFlags perProcessFlags;

  @ApiModelProperty(value = "Overrides for gflags per availability zone")
  private Map<UUID, PerProcessFlags> perAZ = new HashMap<>();

  @ApiModelProperty(
      value = "YbaApi Internal. GFlag groups to be applied",
      example = "[\"ENHANCED_POSTGRES_COMPATIBILITY\"]")
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2024.1.1.0")
  private List<GroupName> gflagGroups = new ArrayList<>();

  public Map<String, String> getGFlags(@Nullable UUID azUuid, UniverseTaskBase.ServerType process) {
    Map<String, String> result = new HashMap<>();
    if (perProcessFlags != null) {
      result.putAll(perProcessFlags.value.getOrDefault(process, new HashMap<>()));
    }
    PerProcessFlags azFlags = perAZ.get(azUuid);
    if (azFlags != null) {
      result.putAll(azFlags.value.getOrDefault(process, new HashMap<>()));
    }
    return result;
  }

  @JsonIgnore
  public void validateConsistency() {
    Collection<UUID> azUuids = new ArrayList<>(Collections.singletonList(null));
    if (perAZ != null) {
      azUuids.addAll(perAZ.keySet());
    }
    for (UUID azUuid : azUuids) {
      GFlagsUtil.checkConsistency(
          getGFlags(azUuid, UniverseTaskBase.ServerType.MASTER),
          getGFlags(azUuid, UniverseTaskBase.ServerType.TSERVER));
    }
  }

  @JsonIgnore
  public SpecificGFlags clone() {
    SpecificGFlags newValue = new SpecificGFlags();
    newValue.setInheritFromPrimary(inheritFromPrimary);
    newValue.perProcessFlags = clone(perProcessFlags);
    if (perAZ != null) {
      perAZ.forEach(
          (k, v) -> {
            newValue.perAZ.put(k, clone(v));
          });
    }
    newValue.gflagGroups = new ArrayList<>(gflagGroups == null ? new ArrayList<>() : gflagGroups);
    return newValue;
  }

  @JsonIgnore
  public void removeGFlag(String gflagKey, UniverseTaskBase.ServerType... serverTypes) {
    if (perProcessFlags != null) {
      for (UniverseTaskBase.ServerType serverType : serverTypes) {
        perProcessFlags.value.getOrDefault(serverType, new HashMap<>()).remove(gflagKey);
      }
    }
  }

  public boolean hasPerAZOverrides() {
    if (MapUtils.isEmpty(perAZ)) {
      return false;
    }
    for (PerProcessFlags value : perAZ.values()) {
      if (value != null && !MapUtils.isEmpty(value.value)) {
        return true;
      }
    }
    return false;
  }

  private static PerProcessFlags clone(PerProcessFlags perProcessFlags) {
    if (perProcessFlags == null) {
      return null;
    }
    PerProcessFlags result = new PerProcessFlags();
    Map<UniverseTaskBase.ServerType, Map<String, String>> cloneValue =
        perProcessFlags.value.entrySet().stream()
            .collect(
                Collectors.toMap(
                    Map.Entry::getKey, // Copy key as is
                    e -> new HashMap<>(e.getValue()) // Deep copy inner map
                    ));

    result.value = new HashMap<>(cloneValue);
    return result;
  }

  public static PerProcessFlags combine(
      PerProcessFlags basePerProcessFlags, PerProcessFlags extraPerProcessFlags) {
    PerProcessFlags finalPerProcessFlags = clone(basePerProcessFlags);
    if (finalPerProcessFlags == null) {
      finalPerProcessFlags = new PerProcessFlags();
    }
    if (extraPerProcessFlags == null) {
      return finalPerProcessFlags;
    }
    for (Map.Entry<UniverseTaskBase.ServerType, Map<String, String>> entry :
        extraPerProcessFlags.value.entrySet()) {
      UniverseTaskBase.ServerType serverType = entry.getKey();
      Map<String, String> flagsMap = entry.getValue();
      if (finalPerProcessFlags.value.containsKey(serverType)) {
        finalPerProcessFlags.value.get(serverType).putAll(flagsMap);
      } else {
        finalPerProcessFlags.value.put(serverType, flagsMap);
      }
    }
    return finalPerProcessFlags;
  }

  public static SpecificGFlags combine(
      SpecificGFlags specificGflags1, SpecificGFlags specificGflags2) {
    if (specificGflags1 == null && specificGflags2 == null) {
      return null;
    }
    if (specificGflags1 == null) {
      specificGflags1 = new SpecificGFlags();
    }
    if (specificGflags2 == null) {
      return specificGflags1;
    }
    SpecificGFlags finalSpecificGFlags = specificGflags1.clone();
    // Add the extra gflag groups.
    List<GroupName> finalGflagGroups = finalSpecificGFlags.getGflagGroups();
    for (GroupName groupName : specificGflags2.gflagGroups) {
      if (!finalGflagGroups.contains(groupName)) {
        finalGflagGroups.add(groupName);
      }
    }
    finalSpecificGFlags.setGflagGroups(finalGflagGroups);

    // Add the per process flags.
    PerProcessFlags finalPerProcessFlags =
        combine(specificGflags1.getPerProcessFlags(), specificGflags2.getPerProcessFlags());
    finalSpecificGFlags.setPerProcessFlags(finalPerProcessFlags);

    // Add the per AZ flags.
    for (Map.Entry<UUID, PerProcessFlags> entry : specificGflags2.getPerAZ().entrySet()) {
      UUID azUUID = entry.getKey();
      PerProcessFlags extraAzPerProcessFlags = entry.getValue();
      if (finalSpecificGFlags.getPerAZ().containsKey(azUUID)) {
        PerProcessFlags baseAzPerProcessFlags = finalSpecificGFlags.getPerAZ().get(azUUID);
        finalSpecificGFlags
            .getPerAZ()
            .put(azUUID, combine(baseAzPerProcessFlags, extraAzPerProcessFlags));
      } else {
        finalSpecificGFlags.getPerAZ().put(azUUID, extraAzPerProcessFlags);
      }
    }

    return finalSpecificGFlags;
  }

  /**
   * Fetches all GFlag keys from the provided SpecificGFlags object. This method aggregates GFlag
   * keys from both per-process and per-AZ (Availability Zone) configurations within the
   * SpecificGFlags object.
   *
   * @param specificGFlags The SpecificGFlags object containing GFlag configurations. Can be null,
   *     in which case an empty set is returned.
   * @return A set of all GFlag keys found in the provided SpecificGFlags object.
   */
  public static Set<String> fetchAllGFlagsFlat(SpecificGFlags specificGFlags) {
    Set<String> allGflagKeys = new HashSet<>();
    if (specificGFlags != null) {
      // Add the per process gflags.
      if (specificGFlags.perProcessFlags != null) {
        for (Map<String, String> value : specificGFlags.perProcessFlags.value.values()) {
          allGflagKeys.addAll(value.keySet());
        }
      }
      // Add the per AZ gflags.
      if (specificGFlags.perAZ != null) {
        for (PerProcessFlags flags : specificGFlags.perAZ.values()) {
          if (flags != null) {
            for (Map<String, String> value : flags.value.values()) {
              allGflagKeys.addAll(value.keySet());
            }
          }
        }
      }
    }
    return allGflagKeys;
  }

  public static boolean isEmpty(SpecificGFlags specificGFlags) {
    if (specificGFlags == null) {
      return true;
    }
    List<PerProcessFlags> allGflags = new ArrayList<>();
    if (specificGFlags.perAZ != null) {
      allGflags.addAll(specificGFlags.perAZ.values());
    }
    if (specificGFlags.getPerProcessFlags() != null) {
      allGflags.add(specificGFlags.getPerProcessFlags());
    }
    for (PerProcessFlags flags : allGflags) {
      if (flags != null && flags.value != null) {
        for (Map<String, String> value : flags.value.values()) {
          if (!value.isEmpty()) {
            return false;
          }
        }
      }
    }
    return true;
  }

  public static SpecificGFlags construct(
      Map<String, String> masterGFlags, Map<String, String> tserverGFlags) {
    SpecificGFlags result = new SpecificGFlags();
    SpecificGFlags.PerProcessFlags perProcessFlags = new SpecificGFlags.PerProcessFlags();
    perProcessFlags.value =
        ImmutableMap.of(
            MASTER, masterGFlags,
            TSERVER, tserverGFlags);
    result.setPerProcessFlags(perProcessFlags);
    return result;
  }

  public static SpecificGFlags constructInherited() {
    SpecificGFlags specificGFlags = new SpecificGFlags();
    specificGFlags.setInheritFromPrimary(true);
    return specificGFlags;
  }
}
