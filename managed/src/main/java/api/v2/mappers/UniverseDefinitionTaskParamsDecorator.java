// Copyright (c) YugaByte, Inc.
package api.v2.mappers;

import api.v2.models.CloudSpecificInfo;
import api.v2.models.EncryptionInTransitSpec;
import api.v2.models.UniverseCreateSpec;
import api.v2.models.UniverseCreateSpecYcql;
import api.v2.models.UniverseCreateSpecYsql;
import api.v2.models.UniverseNetworkingSpec;
import api.v2.models.UniverseSpec;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Provider;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

public abstract class UniverseDefinitionTaskParamsDecorator
    implements UniverseDefinitionTaskParamsMapper {
  private final UniverseDefinitionTaskParamsMapper delegate;

  public UniverseDefinitionTaskParamsDecorator(UniverseDefinitionTaskParamsMapper delegate) {
    this.delegate = delegate;
  }

  // copy some of the details from v1 UniverseDefinitionTaskParams into UniverseSpec
  private void updateUniverseSpec(
      UniverseDefinitionTaskParams v1UniverseTaskParams, UniverseSpec universeSpec) {
    // fetch name of universe from primary cluster and set it at top-level in v2
    UserIntent primaryUserIntent = v1UniverseTaskParams.getPrimaryCluster().userIntent;
    universeSpec.setName(primaryUserIntent.universeName);
    // fetch yb software version from primary cluster and set it at top-level in v2
    universeSpec.setYbSoftwareVersion(primaryUserIntent.ybSoftwareVersion);
    // time sync
    universeSpec.setUseTimeSync(primaryUserIntent.useTimeSync);
    // systemd
    universeSpec.setUseSystemd(primaryUserIntent.useSystemd);
    // fetch networking spec from primary cluster and set it at top-level networking spec in v2
    UniverseNetworkingSpec v2UnivNetworkingSpec = universeSpec.getNetworkingSpec();
    v2UnivNetworkingSpec
        .assignPublicIp(primaryUserIntent.assignPublicIP)
        .assignStaticPublicIp(primaryUserIntent.assignStaticPublicIP)
        .enableIpv6(primaryUserIntent.enableIPV6);
  }

  @Override
  public UniverseCreateSpec toV2UniverseCreateSpec(
      UniverseDefinitionTaskParams v1UniverseTaskParams) {
    UniverseCreateSpec universeSpec = delegate.toV2UniverseCreateSpec(v1UniverseTaskParams);
    // update unmapped properties of spec within created universeSpec
    updateUniverseSpec(v1UniverseTaskParams, universeSpec.getSpec());
    // fetch ysql/ycql password of universe from primary cluster and set it at top-level in v2
    UserIntent primaryUserIntent = v1UniverseTaskParams.getPrimaryCluster().userIntent;
    if (primaryUserIntent.ysqlPassword != null) {
      universeSpec.ysql(new UniverseCreateSpecYsql().password(primaryUserIntent.ysqlPassword));
    }
    if (primaryUserIntent.ycqlPassword != null) {
      universeSpec.ycql(new UniverseCreateSpecYcql().password(primaryUserIntent.ycqlPassword));
    }
    return universeSpec;
  }

  @Override
  public UniverseSpec toV2UniverseSpec(UniverseDefinitionTaskParams v1UniverseTaskParams) {
    UniverseSpec universeSpec = delegate.toV2UniverseSpec(v1UniverseTaskParams);
    updateUniverseSpec(v1UniverseTaskParams, universeSpec);
    return universeSpec;
  }

  @Override
  public UniverseDefinitionTaskParams toV1UniverseDefinitionTaskParams(UniverseSpec universeSpec) {
    UniverseDefinitionTaskParams params = delegate.toV1UniverseDefinitionTaskParams(universeSpec);
    // find out the provider type of universe
    // set universeName, ysqlPassword, ycqlPassword into all cluster's userIntent
    if (params.clusters != null) {
      for (Cluster cluster : params.clusters) {
        if (cluster.userIntent != null) {
          cluster.userIntent.universeName = universeSpec.getName();
          Provider clusterProvider =
              Provider.getOrBadRequest(UUID.fromString(cluster.userIntent.provider));
          cluster.userIntent.providerType = clusterProvider.getCloudCode();
        }
      }
    }
    UserIntent target = params.getPrimaryCluster().userIntent;
    // set yb software version into primary cluster
    if (universeSpec != null && universeSpec.getYbSoftwareVersion() != null) {
      target.ybSoftwareVersion = universeSpec.getYbSoftwareVersion();
    }
    // use time sync
    if (universeSpec != null && universeSpec.getUseTimeSync() != null) {
      target.useTimeSync = universeSpec.getUseTimeSync();
    }
    // use systemd
    if (universeSpec != null && universeSpec.getUseSystemd() != null) {
      target.useSystemd = universeSpec.getUseSystemd();
    }
    // set encryptionInTransit into primary cluster
    if (universeSpec != null && universeSpec.getEncryptionInTransitSpec() != null) {
      EncryptionInTransitSpec source = universeSpec.getEncryptionInTransitSpec();
      target.enableNodeToNodeEncrypt = source.getEnableNodeToNodeEncrypt();
      target.enableClientToNodeEncrypt = source.getEnableClientToNodeEncrypt();
      params.rootCA = source.getRootCa();
      params.setClientRootCA(source.getClientRootCa());
      params.rootAndClientRootCASame =
          source.getRootCa() != null && source.getRootCa().equals(source.getClientRootCa());
    }
    // set networking into primary cluster
    if (universeSpec != null && universeSpec.getNetworkingSpec() != null) {
      UniverseNetworkingSpec source = universeSpec.getNetworkingSpec();
      target.assignPublicIP = source.getAssignPublicIp();
      target.assignStaticPublicIP = source.getAssignStaticPublicIp();
      target.enableIPV6 = source.getEnableIpv6();
    }
    return params;
  }

  @Override
  public UniverseDefinitionTaskParams toV1UniverseDefinitionTaskParamsFromCreateSpec(
      UniverseCreateSpec universeCreateSpec) {
    UniverseDefinitionTaskParams params =
        toV1UniverseDefinitionTaskParams(universeCreateSpec.getSpec());
    // set Arch
    params.arch = toV1Arch(universeCreateSpec.getArch());
    // set ysqlPassword, ycqlPassword into all cluster's userIntent
    if (params.clusters != null) {
      for (Cluster cluster : params.clusters) {
        if (cluster.userIntent != null) {
          if (universeCreateSpec.getYsql() != null) {
            cluster.userIntent.ysqlPassword = universeCreateSpec.getYsql().getPassword();
          }
          if (universeCreateSpec.getYcql() != null) {
            cluster.userIntent.ycqlPassword = universeCreateSpec.getYcql().getPassword();
          }
        }
      }
    }
    return params;
  }

  // Need this due to a MapStruct limitation https://github.com/mapstruct/mapstruct/issues/3165
  // Arrays cannot be mapped to collection. So doing it manually here for lunIndexes
  @Override
  public CloudSpecificInfo toV2CloudSpecificInfo(
      com.yugabyte.yw.models.helpers.CloudSpecificInfo source) {
    CloudSpecificInfo target = delegate.toV2CloudSpecificInfo(source);
    if (source.lun_indexes != null) {
      List<Integer> lunIdx = new ArrayList<>();
      for (Integer lux : source.lun_indexes) {
        lunIdx.add(lux);
      }
      target.setLunIndexes(lunIdx);
    }
    return target;
  }
}
