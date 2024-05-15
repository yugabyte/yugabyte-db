package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.XClusterConfig;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Set;
import javax.annotation.Nullable;
import lombok.Getter;
import lombok.NoArgsConstructor;
import lombok.Setter;
import org.yb.master.MasterDdlOuterClass;
import org.yb.master.MasterDdlOuterClass.ListTablesResponsePB.TableInfo;

@NoArgsConstructor
@Getter
@Setter
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
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList,
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
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList,
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
}
