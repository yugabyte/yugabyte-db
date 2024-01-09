// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.backuprestore;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.protobuf.ByteString;
import com.yugabyte.yw.common.TableSpaceStructures.PlacementBlock;
import com.yugabyte.yw.common.TableSpaceStructures.TableSpaceQueryResponse;
import com.yugabyte.yw.common.TableSpaceUtil;
import com.yugabyte.yw.models.helpers.CommonUtils;
import java.io.IOException;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.validation.Valid;
import javax.validation.constraints.Min;
import javax.validation.constraints.NotNull;
import javax.validation.constraints.Size;
import lombok.Builder;
import lombok.extern.jackson.Jacksonized;
import org.apache.commons.collections4.CollectionUtils;
import org.yb.master.CatalogEntityInfo.PlacementBlockPB;
import org.yb.master.CatalogEntityInfo.PlacementInfoPB;
import org.yb.master.CatalogEntityInfo.ReplicationInfoPB;

@JsonIgnoreProperties(ignoreUnknown = true)
@Builder
@Jacksonized
public class Tablespace {
  @NotNull
  @Size(min = 1)
  @JsonAlias("tablespace_name")
  public String tablespaceName;

  @NotNull
  @Valid
  @JsonAlias("replica_placement")
  public ReplicaPlacement replicaPlacement;

  @Override
  public String toString() {
    return "Tablespace name: " + tablespaceName;
  }

  @JsonIgnore
  public String getJsonString() {
    try {
      ObjectMapper objMapper = new ObjectMapper();
      return objMapper.writeValueAsString(replicaPlacement);
    } catch (JsonProcessingException e) {
      return "";
    }
  }

  @JsonIgnoreProperties(ignoreUnknown = true)
  @Builder
  @Jacksonized
  public static class ReplicaPlacement {
    @NotNull
    @Min(value = 1)
    @JsonAlias("num_replicas")
    public Integer numReplicas;

    @NotNull
    @JsonAlias("placement_blocks")
    @Size(min = 1)
    public List<PlacementBlock> placementBlocks;

    @JsonIgnore
    public ReplicationInfoPB getReplicationInfoPB(UUID placementUUID) {
      List<PlacementBlockPB> placementBlockPBs =
          placementBlocks.stream().map(pB -> pB.getPlacementBlockPB()).collect(Collectors.toList());
      return ReplicationInfoPB.newBuilder()
          .setLiveReplicas(
              PlacementInfoPB.newBuilder()
                  .addAllPlacementBlocks(placementBlockPBs)
                  .setNumReplicas(numReplicas)
                  .setPlacementUuid(ByteString.copyFromUtf8(placementUUID.toString()))
                  .build())
          .build();
    }

    @Override
    public boolean equals(Object obj) {
      if (this == obj) return true;
      if (obj == null) return false;
      if (getClass() != obj.getClass()) return false;
      Tablespace.ReplicaPlacement other = (Tablespace.ReplicaPlacement) obj;
      return numReplicas == other.numReplicas
          && CommonUtils.isEqualIgnoringOrder(placementBlocks, other.placementBlocks);
    }
  }

  // @formatter:off
  /**
   * Get Tablespace object from TableSpaceQueryResponse object.
   * The TableSpaceQueryResponse looks like this: <pre>
   * {@code {
   *     "spcname": "test_1",
   *     "spcoptions": [
   *       "replica_placement={\"num_replicas\":10,\"placement_blocks\":[{\"cloud\":\"aws\",
   *       \"region\":\"us-west-2\",\"zone\":\"us-west-2a\",\"min_num_replicas\":3}"
   *     ]
   *}
   * </pre>
   */
  // @formatter:on
  public static Tablespace getTablespaceFromTablespaceQueryResponse(
      TableSpaceQueryResponse tsQueryResponse) {
    Tablespace.TablespaceBuilder builder = Tablespace.builder();
    builder.tablespaceName(tsQueryResponse.tableSpaceName);
    ObjectMapper objMapper = new ObjectMapper();
    if (CollectionUtils.isNotEmpty(tsQueryResponse.tableSpaceOptions)) {
      for (String tsOptions : tsQueryResponse.tableSpaceOptions) {
        if (tsOptions.startsWith(TableSpaceUtil.REPLICA_PLACEMENT_TEXT)) {
          String optStr = tsOptions.replace(TableSpaceUtil.REPLICA_PLACEMENT_TEXT, "");
          try {
            Tablespace.ReplicaPlacement replicaPlacement =
                objMapper.readValue(optStr, Tablespace.ReplicaPlacement.class);
            builder.replicaPlacement(replicaPlacement);
            return builder.build();
          } catch (IOException e) {
            throw new RuntimeException(
                String.format("Tablespace %s parsing failed.", tsQueryResponse.tableSpaceName));
          }
        }
      }
      throw new RuntimeException(
          String.format(
              "Tablespace %s does not contain replica placement info.",
              tsQueryResponse.tableSpaceName));
    } else {
      throw new RuntimeException(
          String.format(
              "Tablespace %s does not contain 'spcoptions'.", tsQueryResponse.tableSpaceName));
    }
  }
}
