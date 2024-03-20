// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.gflags;

import static com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType.MASTER;
import static com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType.TSERVER;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import javax.annotation.Nullable;
import lombok.Data;
import lombok.EqualsAndHashCode;
import org.apache.commons.collections4.MapUtils;

@Data
@ApiModel(description = "GFlags for current cluster")
public class SpecificGFlags {

  @EqualsAndHashCode
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

  private PerProcessFlags clone(PerProcessFlags perProcessFlags) {
    if (perProcessFlags == null) {
      return null;
    }
    PerProcessFlags result = new PerProcessFlags();
    result.value = new HashMap<>(perProcessFlags.value);
    return result;
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
