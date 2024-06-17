// Copyright (c) YugaByte, Inc.
package api.v2.mappers;

import api.v2.models.CloudSpecificInfo;
import api.v2.models.ClusterSpec;
import api.v2.models.ClusterSpec.ClusterTypeEnum;
import api.v2.models.EncryptionInTransitSpec;
import api.v2.models.UniverseCreateSpec;
import api.v2.models.UniverseNetworkingSpec;
import api.v2.models.UniverseSpec;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Provider;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import org.mapstruct.factory.Mappers;

public abstract class UniverseDefinitionTaskParamsDecorator
    implements UniverseDefinitionTaskParamsMapper {
  private final UniverseDefinitionTaskParamsMapper delegate;
  private final ClusterMapper clusterMapper = Mappers.getMapper(ClusterMapper.class);

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

    // set rootCA of encryptionInTransit into top-level of v1 universe
    if (universeSpec != null && universeSpec.getEncryptionInTransitSpec() != null) {
      EncryptionInTransitSpec source = universeSpec.getEncryptionInTransitSpec();
      params.rootCA = source.getRootCa();
      params.setClientRootCA(source.getClientRootCa());
      params.rootAndClientRootCASame =
          source.getRootCa() != null && source.getRootCa().equals(source.getClientRootCa());
    }

    // The generated mapper will only create v1 Cluster from corresponding v2Cluster. Copy
    // v1 cluster properties from top-level of UniverseSpec.
    if (params.clusters != null) {
      for (int i = 0; i < params.clusters.size(); i++) {
        Cluster cluster = params.clusters.get(i);
        if (cluster.userIntent != null) {
          // set universeName into all clusters
          if (universeSpec != null) {
            cluster.userIntent.universeName = universeSpec.getName();
          }
          // set the provider type for each cluster
          Provider clusterProvider =
              Provider.getOrBadRequest(UUID.fromString(cluster.userIntent.provider));
          cluster.userIntent.providerType = clusterProvider.getCloudCode();
          // set yb software version into all clusters
          if (universeSpec != null && universeSpec.getYbSoftwareVersion() != null) {
            cluster.userIntent.ybSoftwareVersion = universeSpec.getYbSoftwareVersion();
          }
          // use time sync into all clusters
          if (universeSpec != null && universeSpec.getUseTimeSync() != null) {
            cluster.userIntent.useTimeSync = universeSpec.getUseTimeSync();
          }
          // use systemd into all clusters
          if (universeSpec != null && universeSpec.getUseSystemd() != null) {
            cluster.userIntent.useSystemd = universeSpec.getUseSystemd();
          }
          // set encryptionInTransit into all clusters
          if (universeSpec != null && universeSpec.getEncryptionInTransitSpec() != null) {
            EncryptionInTransitSpec source = universeSpec.getEncryptionInTransitSpec();
            cluster.userIntent.enableNodeToNodeEncrypt = source.getEnableNodeToNodeEncrypt();
            cluster.userIntent.enableClientToNodeEncrypt = source.getEnableClientToNodeEncrypt();
          }
          // set networking into all clusters
          if (universeSpec != null && universeSpec.getNetworkingSpec() != null) {
            UniverseNetworkingSpec source = universeSpec.getNetworkingSpec();
            cluster.userIntent.assignPublicIP = source.getAssignPublicIp();
            cluster.userIntent.assignStaticPublicIP = source.getAssignStaticPublicIp();
            cluster.userIntent.enableIPV6 = source.getEnableIpv6();
          }
          // set ysql into all clusters
          if (universeSpec != null && universeSpec.getYsql() != null) {
            cluster.userIntent.enableYSQL = universeSpec.getYsql().getEnable();
            cluster.userIntent.enableYSQLAuth = universeSpec.getYsql().getEnableAuth();
            cluster.userIntent.ysqlPassword = universeSpec.getYsql().getPassword();
          }
          // set ycql into all clusters
          if (universeSpec != null && universeSpec.getYcql() != null) {
            cluster.userIntent.enableYCQL = universeSpec.getYcql().getEnable();
            cluster.userIntent.enableYCQLAuth = universeSpec.getYcql().getEnableAuth();
            cluster.userIntent.ycqlPassword = universeSpec.getYcql().getPassword();
          }
        }
      }
    }
    return params;
  }

  private UniverseSpec inheritFromPrimaryBeforeMapping(UniverseSpec universeSpec) {
    if (universeSpec == null || universeSpec.getClusters() == null) {
      return universeSpec;
    }
    ClusterSpec primaryClusterSpec =
        universeSpec.getClusters().stream()
            .filter(c -> c.getClusterType().equals(ClusterTypeEnum.PRIMARY))
            .findAny()
            .orElse(null);
    List<ClusterSpec> clusters = new ArrayList<>();
    clusters.add(primaryClusterSpec);
    for (ClusterSpec otherCluster : universeSpec.getClusters()) {
      if (!otherCluster.getClusterType().equals(ClusterTypeEnum.PRIMARY)) {
        ClusterSpec mergedClusterSpec = new ClusterSpec();
        clusterMapper.deepCopyClusterSpecWithoutPlacementSpec(
            primaryClusterSpec, mergedClusterSpec);
        clusterMapper.deepCopyClusterSpec(otherCluster, mergedClusterSpec);
        clusters.add(mergedClusterSpec);
      }
    }
    universeSpec.clusters(clusters);
    return universeSpec;
  }

  @Override
  public UniverseDefinitionTaskParams toV1UniverseDefinitionTaskParamsFromCreateSpec(
      UniverseCreateSpec universeCreateSpec) {
    // copy over unintialized properties of RR cluster from primary cluster
    UniverseSpec universeSpec = inheritFromPrimaryBeforeMapping(universeCreateSpec.getSpec());
    UniverseDefinitionTaskParams params = toV1UniverseDefinitionTaskParams(universeSpec);
    // set Arch
    params.arch = toV1Arch(universeCreateSpec.getArch());
    // set ysqlPassword, ycqlPassword into all cluster's userIntent
    if (params.clusters != null) {
      for (Cluster cluster : params.clusters) {
        if (cluster.userIntent != null) {
          if (universeCreateSpec.getSpec().getYsql() != null) {
            cluster.userIntent.ysqlPassword = universeCreateSpec.getSpec().getYsql().getPassword();
          }
          if (universeCreateSpec.getSpec().getYcql() != null) {
            cluster.userIntent.ycqlPassword = universeCreateSpec.getSpec().getYcql().getPassword();
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
