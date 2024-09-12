package com.yugabyte.yw.forms;

import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData.BootstrapParams;
import com.yugabyte.yw.forms.XClusterConfigRestartFormData.RestartBootstrapParams;
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.XClusterConfig;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import javax.annotation.Nullable;
import lombok.Getter;
import lombok.NoArgsConstructor;
import lombok.Setter;
import org.yb.master.MasterDdlOuterClass.ListTablesResponsePB.TableInfo;

@NoArgsConstructor
@Getter
@Setter
@JsonDeserialize(converter = DrConfigTaskParams.Converter.class)
public class DrConfigTaskParams extends XClusterConfigTaskParams {

  protected DrConfig drConfig;
  protected XClusterConfig oldXClusterConfig;
  protected Map<String, Long> namespaceIdSafetimeEpochUsMap;

  /** It is used in the create method. */
  public DrConfigTaskParams(
      DrConfig drConfig,
      XClusterConfigCreateFormData.BootstrapParams bootstrapParams,
      List<TableInfo> tableInfoList,
      Map<String, List<String>> mainTableIndexTablesMap,
      Map<String, String> sourceTableIdTargetTableIdMap,
      DrConfigCreateForm.PitrParams pitrParams) {
    super(
        drConfig.getActiveXClusterConfig(),
        bootstrapParams,
        tableInfoList,
        mainTableIndexTablesMap,
        sourceTableIdTargetTableIdMap,
        pitrParams);
    this.drConfig = drConfig;
  }

  /** DB scoped replication create method */
  public DrConfigTaskParams(
      DrConfig drConfig,
      XClusterConfigCreateFormData.BootstrapParams bootstrapParams,
      Set<String> dbs,
      DrConfigCreateForm.PitrParams pitrParams) {
    super(drConfig.getActiveXClusterConfig(), bootstrapParams, dbs, pitrParams);
    this.drConfig = drConfig;
  }

  /** It is used in the delete method. */
  public DrConfigTaskParams(DrConfig drConfig, boolean isForced) {
    super(drConfig.getActiveXClusterConfig(), isForced);
    this.drConfig = drConfig;
  }

  /** It is used in the replaceReplica methods. */
  public DrConfigTaskParams(
      DrConfig drConfig,
      @Nullable XClusterConfig oldXClusterConfig,
      XClusterConfig newXClusterConfig,
      XClusterConfigCreateFormData.BootstrapParams bootstrapParams,
      List<TableInfo> tableInfoList,
      Map<String, List<String>> mainTableIndexTablesMap,
      Map<String, String> sourceTableIdTargetTableIdMap) {
    super(
        newXClusterConfig,
        bootstrapParams,
        tableInfoList,
        mainTableIndexTablesMap,
        sourceTableIdTargetTableIdMap);
    this.drConfig = drConfig;
    this.oldXClusterConfig = oldXClusterConfig;
  }

  /** It is used in the switchover and failover methods. */
  public DrConfigTaskParams(
      DrConfig drConfig,
      @Nullable XClusterConfig oldXClusterConfig,
      XClusterConfig newXClusterConfig,
      @Nullable Map<String, Long> namespaceIdSafetimeEpochUsMap,
      List<TableInfo> tableInfoList,
      Map<String, List<String>> mainTableIndexTablesMap) {
    super(
        newXClusterConfig,
        null /* bootstrapParams */,
        tableInfoList,
        mainTableIndexTablesMap,
        Collections.emptyMap() /* sourceTableIdTargetTableIdMap */);
    this.drConfig = drConfig;
    this.oldXClusterConfig = oldXClusterConfig;
    this.namespaceIdSafetimeEpochUsMap = namespaceIdSafetimeEpochUsMap;
  }

  public DrConfigTaskParams(
      DrConfig drConfig,
      @Nullable XClusterConfig oldXClusterConfig,
      XClusterConfig newXClusterConfig,
      Set<String> dbs,
      @Nullable Map<String, Long> namespaceIdSafetimeEpochUsMap) {
    super(
        newXClusterConfig,
        null /* bootstrapParams */,
        Collections.emptyList(), /* tableInfoRespList */
        Collections.emptyMap(), /* mainTableIndexTablesMap */
        Collections.emptyMap() /* sourceTableIdTargetTableIdMap */);
    this.drConfig = drConfig;
    this.dbs = new HashSet<>(dbs);
    this.oldXClusterConfig = oldXClusterConfig;
    this.namespaceIdSafetimeEpochUsMap = namespaceIdSafetimeEpochUsMap;
  }

  public DrConfigTaskParams(
      DrConfig drConfig,
      RestartBootstrapParams bootstrapParams,
      DrConfigCreateForm.PitrParams pitrParams) {
    super(drConfig.getActiveXClusterConfig());
    this.drConfig = drConfig;
    if (bootstrapParams != null) {
      this.bootstrapParams = new BootstrapParams();
      this.bootstrapParams.backupRequestParams = bootstrapParams.backupRequestParams;
    }
    this.pitrParams = pitrParams;
  }

  @Override
  public void refreshIfExists() {
    super.refreshIfExists();
    if (Objects.nonNull(drConfig)) {
      drConfig = DrConfig.maybeGet(drConfig.getUuid()).orElse(null);
    }
    if (Objects.nonNull(oldXClusterConfig)) {
      oldXClusterConfig = XClusterConfig.maybeGet(oldXClusterConfig.getUuid()).orElse(null);
    }
  }

  public static class Converter extends BaseConverter<DrConfigTaskParams> {}
}
