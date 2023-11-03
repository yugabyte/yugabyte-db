package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.XClusterConfig;
import java.util.List;
import java.util.Map;
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
  protected DrConfigFailoverForm.Type failoverType;
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

  /** It is used in the delete method. */
  public DrConfigTaskParams(DrConfig drConfig, boolean isForced) {
    super(drConfig.getActiveXClusterConfig(), isForced);
    this.drConfig = drConfig;
  }

  /** It is used in the edit and failover methods. */
  public DrConfigTaskParams(
      DrConfig drConfig,
      @Nullable DrConfigFailoverForm.Type failoverType,
      @Nullable Map<String, Long> namespaceIdSafetimeEpochUsMap,
      XClusterConfig xClusterConfig,
      @Nullable XClusterConfigCreateFormData.BootstrapParams bootstrapParams,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList,
      Map<String, List<String>> mainTableIndexTablesMap,
      Map<String, String> sourceTableIdTargetTableIdMap) {
    super(
        xClusterConfig,
        bootstrapParams,
        tableInfoList,
        mainTableIndexTablesMap,
        sourceTableIdTargetTableIdMap);
    this.drConfig = drConfig;
    this.failoverType = failoverType;
    this.namespaceIdSafetimeEpochUsMap = namespaceIdSafetimeEpochUsMap;
  }
}
