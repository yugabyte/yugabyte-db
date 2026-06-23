// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import api.v2.models.DrConfigInfo;
import api.v2.models.DrConfigSpec;
import api.v2.models.DrConfigUniverseReplicationState;
import api.v2.models.TableType;
import com.yugabyte.yw.common.DrConfigStates.SourceUniverseState;
import com.yugabyte.yw.common.DrConfigStates.State;
import com.yugabyte.yw.common.DrConfigStates.TargetUniverseState;
import com.yugabyte.yw.forms.DrConfigGetResp;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import org.mapstruct.Mapper;
import org.mapstruct.MappingConstants;
import org.mapstruct.ReportingPolicy;
import org.mapstruct.ValueMapping;
import org.mapstruct.ValueMappings;
import org.mapstruct.factory.Mappers;

@Mapper(unmappedSourcePolicy = ReportingPolicy.ERROR)
public interface DrConfigEnumMapper {

  DrConfigEnumMapper INSTANCE = Mappers.getMapper(DrConfigEnumMapper.class);

  @ValueMappings({
    @ValueMapping(target = "UNKNOWN", source = "UNKNOWN"),
    @ValueMapping(target = "YSQL", source = "YSQL"),
    @ValueMapping(target = "YCQL", source = "YCQL")
  })
  TableType toTableTypeEnum(com.yugabyte.yw.models.XClusterConfig.TableType tableType);

  @ValueMappings({
    @ValueMapping(target = "BASIC", source = "Basic"),
    @ValueMapping(target = "TXN", source = "Txn"),
    @ValueMapping(target = "DB", source = "Db")
  })
  DrConfigSpec.TypeEnum toTypeEnum(ConfigType type);

  default DrConfigSpec.TypeEnum toDrConfigSpecType(DrConfigGetResp source) {
    if (source.getType() == ConfigType.Db && Boolean.TRUE.equals(source.isAutomaticDdlMode())) {
      return DrConfigSpec.TypeEnum.AUTO_DDL;
    }
    return toTypeEnum(source.getType());
  }

  @ValueMappings({
    @ValueMapping(target = "INITIALIZING", source = "Initializing"),
    @ValueMapping(target = "REPLICATING", source = "Replicating"),
    @ValueMapping(target = "SWITCHOVER_IN_PROGRESS", source = "SwitchoverInProgress"),
    @ValueMapping(target = "FAILOVER_IN_PROGRESS", source = "FailoverInProgress"),
    @ValueMapping(target = "PAUSED", source = "Paused"),
    @ValueMapping(target = "HALTED", source = "Halted"),
    @ValueMapping(target = "UPDATING", source = "Updating"),
    @ValueMapping(target = "FAILED", source = "Failed")
  })
  DrConfigInfo.StateEnum toStateEnum(State state);

  /** xCluster statuses that override all DR workflow states. */
  @ValueMappings({
    @ValueMapping(target = "DELETED_UNIVERSE", source = "DeletedUniverse"),
    @ValueMapping(target = "DELETION_FAILED", source = "DeletionFailed"),
    @ValueMapping(target = MappingConstants.NULL, source = "Initialized"),
    @ValueMapping(target = MappingConstants.NULL, source = "Running"),
    @ValueMapping(target = MappingConstants.NULL, source = "Updating"),
    @ValueMapping(target = MappingConstants.NULL, source = "Failed"),
    @ValueMapping(target = MappingConstants.NULL, source = "DrainedData")
  })
  DrConfigInfo.StateEnum toXClusterInfrastructureState(XClusterConfigStatusType status);

  /** DR workflow states that take precedence over xCluster activity signals. */
  @ValueMappings({
    @ValueMapping(target = "INITIALIZING", source = "Initializing"),
    @ValueMapping(target = "SWITCHOVER_IN_PROGRESS", source = "SwitchoverInProgress"),
    @ValueMapping(target = "FAILOVER_IN_PROGRESS", source = "FailoverInProgress"),
    @ValueMapping(target = "HALTED", source = "Halted"),
    @ValueMapping(target = "PAUSED", source = "Paused"),
    @ValueMapping(target = MappingConstants.NULL, source = "Replicating"),
    @ValueMapping(target = MappingConstants.NULL, source = "Updating"),
    @ValueMapping(target = MappingConstants.NULL, source = "Failed")
  })
  DrConfigInfo.StateEnum toActiveDrWorkflowState(State state);

  /** xCluster-derived state when no DR state is available. */
  @ValueMappings({
    @ValueMapping(target = "FAILED", source = "Failed"),
    @ValueMapping(target = "UPDATING", source = "Updating"),
    @ValueMapping(target = "UPDATING", source = "Initialized"),
    @ValueMapping(target = MappingConstants.NULL, source = "Running"),
    @ValueMapping(target = MappingConstants.NULL, source = "DeletedUniverse"),
    @ValueMapping(target = MappingConstants.NULL, source = "DeletionFailed"),
    @ValueMapping(target = MappingConstants.NULL, source = "DrainedData")
  })
  DrConfigInfo.StateEnum toXClusterFallbackState(XClusterConfigStatusType status);

  /**
   * Derives the public DR config state from internal DR workflow state and xCluster status.
   * Single-source mappings use {@link ValueMappings}; cross-source precedence is handled here.
   */
  default DrConfigInfo.StateEnum toCombinedStateEnum(DrConfigGetResp source) {
    return toCombinedStateEnum(source.getStatus(), source.getState());
  }

  default DrConfigInfo.StateEnum toCombinedStateEnum(
      XClusterConfigStatusType xClusterStatus, State drState) {
    DrConfigInfo.StateEnum infrastructure = toXClusterInfrastructureState(xClusterStatus);

    if (infrastructure != null) {
      return infrastructure;
    }

    DrConfigInfo.StateEnum activeDr = toActiveDrWorkflowState(drState);
    if (activeDr != null) {
      return activeDr;
    }

    if (drState == State.Failed || xClusterStatus == XClusterConfigStatusType.Failed) {
      return DrConfigInfo.StateEnum.FAILED;
    }

    if (drState == State.Updating
        || xClusterStatus == XClusterConfigStatusType.Updating
        || xClusterStatus == XClusterConfigStatusType.Initialized) {
      return DrConfigInfo.StateEnum.UPDATING;
    }

    if (drState == State.Replicating) {
      return DrConfigInfo.StateEnum.REPLICATING;
    }

    if (drState != null) {
      return toStateEnum(drState);
    }

    return toXClusterFallbackState(xClusterStatus);
  }

  @ValueMappings({
    @ValueMapping(target = "UNCONFIGURED_FOR_DR", source = "Unconfigured"),
    @ValueMapping(target = "READY_TO_REPLICATE", source = "ReadyToReplicate"),
    @ValueMapping(target = "WAITING_FOR_DR", source = "WaitingForDr"),
    @ValueMapping(target = "REPLICATING_DATA", source = "ReplicatingData"),
    @ValueMapping(target = "REPLICATION_PAUSED", source = "ReplicationPaused"),
    @ValueMapping(target = "PREPARING_FOR_SWITCHOVER", source = "PreparingSwitchover"),
    @ValueMapping(target = "SWITCHING_TO_DR_REPLICA", source = "SwitchingToDrReplica"),
    @ValueMapping(target = "UNIVERSE_MARKED_AS_DR_FAILED", source = "DrFailed")
  })
  DrConfigUniverseReplicationState toPrimaryUniverseState(SourceUniverseState state);

  @ValueMappings({
    @ValueMapping(target = "UNCONFIGURED_FOR_DR", source = "Unconfigured"),
    @ValueMapping(target = "BOOTSTRAPPING", source = "Bootstrapping"),
    @ValueMapping(target = "RECEIVING_DATA_READY_FOR_READS", source = "ReceivingData"),
    @ValueMapping(target = "REPLICATION_PAUSED", source = "ReplicationPaused"),
    @ValueMapping(target = "SWITCHING_TO_DR_PRIMARY", source = "SwitchingToDrPrimary"),
    @ValueMapping(target = "UNIVERSE_MARKED_AS_DR_FAILED", source = "DrFailed")
  })
  DrConfigUniverseReplicationState toReplicaUniverseState(TargetUniverseState state);
}
