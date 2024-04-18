// Copyright (c) YugaByte, Inc.
package api.v2.mappers;

import api.v2.models.EncryptionAtRestSpec;
import api.v2.models.EncryptionAtRestSpec.OpTypeEnum;
import api.v2.models.EncryptionInTransitSpec;
import api.v2.models.UniverseCreateSpec;
import api.v2.models.UniverseCreateSpecYcql;
import api.v2.models.UniverseCreateSpecYsql;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;

public abstract class UniverseDefinitionTaskParamsDecorator
    implements UniverseDefinitionTaskParamsMapper {
  private final UniverseDefinitionTaskParamsMapper delegate;

  public UniverseDefinitionTaskParamsDecorator(UniverseDefinitionTaskParamsMapper delegate) {
    this.delegate = delegate;
  }

  @Override
  public UniverseCreateSpec toV2UniverseCreateSpec(
      UniverseDefinitionTaskParams v1UniverseTaskParams) {
    UniverseCreateSpec universeSpec = delegate.toV2UniverseCreateSpec(v1UniverseTaskParams);
    // fetch name of universe from primary cluster and set it at top-level in v2
    UserIntent primaryUserIntent = v1UniverseTaskParams.getPrimaryCluster().userIntent;
    universeSpec.getSpec().setName(primaryUserIntent.universeName);
    // fetch ysql/ycql password of universe from primary cluster and set it at top-level in v2
    if (primaryUserIntent.ysqlPassword != null) {
      universeSpec.ysql(new UniverseCreateSpecYsql().password(primaryUserIntent.ysqlPassword));
    }
    if (primaryUserIntent.ycqlPassword != null) {
      universeSpec.ycql(new UniverseCreateSpecYcql().password(primaryUserIntent.ycqlPassword));
    }
    return universeSpec;
  }

  @Override
  public UniverseDefinitionTaskParams toV1UniverseDefinitionTaskParamsFromCreateSpec(
      UniverseCreateSpec universeCreateSpec) {
    UniverseDefinitionTaskParams params =
        delegate.toV1UniverseDefinitionTaskParamsFromCreateSpec(universeCreateSpec);
    // set universeName, ysqlPassword, ycqlPassword into all cluster's userIntent
    if (params.clusters != null) {
      for (Cluster cluster : params.clusters) {
        if (cluster.userIntent != null) {
          cluster.userIntent.universeName = universeCreateSpec.getSpec().getName();
          if (universeCreateSpec.getYsql() != null) {
            cluster.userIntent.ysqlPassword = universeCreateSpec.getYsql().getPassword();
          }
          if (universeCreateSpec.getYcql() != null) {
            cluster.userIntent.ycqlPassword = universeCreateSpec.getYcql().getPassword();
          }
        }
      }
    }
    // set encryptionAtRestConfig
    if (universeCreateSpec.getSpec() != null
        && universeCreateSpec.getSpec().getEncryptionAtRestSpec() != null) {
      EncryptionAtRestSpec encryptionAtRestSpec =
          universeCreateSpec.getSpec().getEncryptionAtRestSpec();
      params.encryptionAtRestConfig = delegate.toV1EncryptionAtRestConfig(encryptionAtRestSpec);
      params.encryptionAtRestConfig.encryptionAtRestEnabled =
          encryptionAtRestSpec != null && encryptionAtRestSpec.getOpType() == OpTypeEnum.ENABLE;
    }
    // set encryptionInTransit
    if (universeCreateSpec.getSpec() != null
        && universeCreateSpec.getSpec().getEncryptionInTransitSpec() != null) {
      EncryptionInTransitSpec source = universeCreateSpec.getSpec().getEncryptionInTransitSpec();
      UserIntent target = params.getPrimaryCluster().userIntent;
      target.enableNodeToNodeEncrypt = source.getEnableNodeToNodeEncrypt();
      target.enableClientToNodeEncrypt = source.getEnableClientToNodeEncrypt();
      params.rootCA = source.getRootCa();
      params.setClientRootCA(source.getClientRootCa());
      params.rootAndClientRootCASame =
          source.getRootCa() != null && source.getRootCa().equals(source.getClientRootCa());
    }
    return params;
  }
}
