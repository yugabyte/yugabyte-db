package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.DrConfig;
import java.util.List;
import java.util.Map;
import lombok.Getter;
import lombok.NoArgsConstructor;
import lombok.Setter;
import org.yb.master.MasterDdlOuterClass.ListTablesResponsePB.TableInfo;

@NoArgsConstructor
@Getter
@Setter
public class DrConfigTaskParams extends XClusterConfigTaskParams {

  protected DrConfig drConfig;

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

  public DrConfigTaskParams(DrConfig drConfig, boolean isForced) {
    super(drConfig.getActiveXClusterConfig(), isForced);
    this.drConfig = drConfig;
  }
}
