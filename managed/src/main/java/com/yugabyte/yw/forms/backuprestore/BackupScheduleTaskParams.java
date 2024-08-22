// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms.backuprestore;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.Universe;
import java.util.UUID;
import lombok.Getter;
import lombok.Setter;
import org.yb.CommonTypes;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = BackupScheduleTaskParams.Converter.class)
@Getter
@Setter
public class BackupScheduleTaskParams extends UpgradeTaskParams {
  public UUID scheduleUUID;
  public UUID customerUUID;
  public BackupRequestParams scheduleParams;
  public static final UpgradeOption upgradeOption = UpgradeOption.NON_RESTART_UPGRADE;

  @Override
  public boolean isKubernetesUpgradeSupported() {
    return true;
  }

  @Override
  public void verifyParams(Universe universe, boolean isFirstTry) {
    super.verifyParams(universe, isFirstTry);
    if (this.scheduleParams.enablePointInTimeRestore
        && universe.getUniverseDetails().getPrimaryCluster().userIntent.providerType
            == CloudType.kubernetes) {
      String softwareVersion =
          universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
      if (!KubernetesUtil.isNonRestartGflagsUpgradeSupported(softwareVersion)) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "Software version %s does not support Point In Time Recovery enabled backup"
                    + " schedules",
                softwareVersion));
      }
    }
    verifyDbAPIsRunning(universe.getUniverseDetails().getPrimaryCluster().userIntent);
  }

  private void verifyDbAPIsRunning(UserIntent userIntent) {
    if (this.scheduleParams.backupType != null) {
      if (this.scheduleParams.backupType.equals(CommonTypes.TableType.PGSQL_TABLE_TYPE)
          && !userIntent.enableYSQL) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Cannot take backups on YSQL tables if API is disabled");
      }
      if (this.scheduleParams.backupType.equals(CommonTypes.TableType.YQL_TABLE_TYPE)
          && !userIntent.enableYCQL) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Cannot take backups on YCQL tables if API is disabled");
      }
    }
  }

  public static class Converter extends BaseConverter<BackupScheduleTaskParams> {}
}
