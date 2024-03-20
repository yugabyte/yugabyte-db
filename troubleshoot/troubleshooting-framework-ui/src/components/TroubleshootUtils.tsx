import { isNonEmptyArray } from '../utils/objectUtils';

export const formatData = (data: any) => {
  const formattedData =
    'universeDetails' in data ? formatUniverseDetails(data) : formatClusterDetails(data);
  return formattedData;
};

export const formatUniverseDetails = (universeData: any) => {
  const primaryClusterMapping: any = new Map();
  const asyncClusterMapping: any = new Map();

  const primaryZoneMapping: any = new Map();
  const asyncZoneMapping: any = new Map();
  const nodeIPMapping: any = new Map();

  const universeDetails = universeData?.universeDetails;
  if (universeDetails) {
    const nodeDetailsSet = universeDetails.nodeDetailsSet;
    const clusters = universeDetails.clusters;
    const primaryCluster = getPrimaryCluster(clusters);
    const asyncCluster = getReadOnlyCluster(clusters);

    const primaryClusterRegionList = primaryCluster.placementInfo.cloudList[0].regionList;
    const asyncClusterRegionList = asyncCluster?.placementInfo?.cloudList[0]?.regionList;

    const primaryClusterUuid = primaryCluster.uuid;
    const asyncClusterUuid = asyncCluster?.uuid;
    const primaryClusterRegions = primaryClusterRegionList.map((region: any) => region.code);
    const asyncClusterRegions = asyncClusterRegionList?.map((region: any) => region.code);

    primaryClusterMapping.set('Primary', {
      cluster: 'Primary',
      regions: primaryClusterRegions,
      uuid: primaryClusterUuid
    });

    if (asyncCluster) {
      asyncClusterMapping.set('Async', {
        cluster: 'Read Replica',
        regions: asyncClusterRegions,
        uuid: asyncClusterUuid
      });
    }

    for (const { cloudInfo, placementUuid, nodeName } of nodeDetailsSet) {
      nodeIPMapping.set(nodeName, {
        nodeName: nodeName,
        ip: cloudInfo.private_ip
      });
      if (placementUuid === primaryClusterUuid) {
        primaryZoneMapping.set(cloudInfo.az, {
          zoneName: cloudInfo.az,
          nodeNames: getNodesBasedOnZonesYBA(nodeDetailsSet, cloudInfo.az, primaryClusterUuid),
          isReadReplica: false,
          regionName: cloudInfo.region
        });
      } else {
        asyncZoneMapping.set(cloudInfo.az, {
          zoneName: cloudInfo.az,
          nodeNames: getNodesBasedOnZonesYBA(nodeDetailsSet, cloudInfo.az, asyncClusterUuid),
          isReadReplica: true,
          regionName: cloudInfo.region
        });
      }
    }
  }

  return {
    primaryZoneMapping,
    asyncZoneMapping,
    primaryClusterMapping,
    asyncClusterMapping,
    nodeIPMapping
  };
};

export const formatClusterDetails = (nodeData: any) => {
  const primaryClusterMapping: any = new Map();
  const asyncClusterMapping: any = new Map();

  const primaryZoneMapping: any = new Map();
  const asyncZoneMapping: any = new Map();

  const nodeDetails = nodeData?.data;
  if (nodeDetails) {
    for (const { cloud_info, is_read_replica } of nodeDetails) {
      !is_read_replica
        ? primaryClusterMapping.set('Primary', {
            cluster: 'Primary',
            regions: getRegionsBasedOnCluster(nodeDetails, false)
          })
        : asyncClusterMapping.set('Async', {
            cluster: 'Read Replica',
            regions: getRegionsBasedOnCluster(nodeDetails, true)
          });

      !is_read_replica
        ? primaryZoneMapping.set(cloud_info.zone, {
            zoneName: cloud_info.zone,
            nodeNames: getNodesBasedOnZones(nodeDetails, cloud_info.zone, false),
            isReadReplica: is_read_replica,
            regionName: cloud_info.region
          })
        : asyncZoneMapping.set(cloud_info.zone, {
            zoneName: cloud_info.zone,
            nodeNames: getNodesBasedOnZones(nodeDetails, cloud_info.zone, true),
            isReadReplica: is_read_replica,
            regionName: cloud_info.region
          });
    }
  }

  return {
    primaryZoneMapping,
    asyncZoneMapping,
    primaryClusterMapping,
    asyncClusterMapping
  };
};

export const getNodesBasedOnZones = (
  nodeDetailsData: any,
  zoneName: string,
  isReadReplica: boolean
) => {
  const nodeNames: Array<string> = [];
  for (const { name, cloud_info, is_read_replica } of nodeDetailsData) {
    if (zoneName === cloud_info.zone && is_read_replica === isReadReplica) nodeNames.push(name);
  }
  return nodeNames;
};

export const getNodesBasedOnZonesYBA = (nodeDetailsData: any, zoneName: string, uuid: string) => {
  const nodeNames: Array<string> = [];
  for (const { nodeName, cloudInfo, placementUuid } of nodeDetailsData) {
    if (zoneName === cloudInfo.az && placementUuid === uuid) nodeNames.push(nodeName);
  }
  return nodeNames;
};

export const getRegionsBasedOnCluster = (nodeDetailsData: any, isReadReplica: boolean) => {
  const regions: Array<string> = [];
  for (const { cloud_info, is_read_replica } of nodeDetailsData) {
    if (is_read_replica === isReadReplica && !regions.includes(cloud_info.region)) {
      regions.push(cloud_info.region);
    }
  }
  return regions;
};

export const getFilteredItems = (
  zoneToNodesMap: any,
  isRegionChanged: boolean,
  isPrimaryCluster: boolean,
  filterParam: string
) => {
  const zoneToNodesList = Array.from(zoneToNodesMap);
  const filteredList = zoneToNodesList.filter((item: any) => {
    return isRegionChanged
      ? item[1].regionName === filterParam
      : isPrimaryCluster
      ? !item[1].isReadReplica
      : item[1].isReadReplica;
  });
  const filteredMap = filteredList
    ? new Map(filteredList.map((obj: any) => [obj[0], obj[1]]))
    : new Map();
  return filteredMap;
};

export const getPrimaryCluster = (clusters: any) => {
  if (isNonEmptyArray(clusters)) {
    const foundClusters = clusters.filter((cluster: any) => cluster.clusterType === 'PRIMARY');
    if (foundClusters.length === 1) {
      return foundClusters[0];
    }
  }
  return null;
};

export function getReadOnlyCluster(clusters: any) {
  if (isNonEmptyArray(clusters)) {
    const foundClusters = clusters.filter((cluster: any) => cluster.clusterType === 'ASYNC');
    if (foundClusters.length === 1) {
      return foundClusters[0];
    }
  }
  return null;
}
