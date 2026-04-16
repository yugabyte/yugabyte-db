import { isNonEmptyArray, isNonEmptyObject } from '@yugabytedb/ui-components';
import { MIN_OUTLIER_NUM_NODES } from "./constants";
import { Anomaly, AppName, GraphLabel, GraphMetadata, MetricMeasure, RCAGuideline, SplitMode, SplitType, TroubleshootingRecommendations } from "./dtos";

export const formatData = (data: any, appName: AppName) => {
  const formattedData = 
    appName === AppName.YBA ? formatUniverseDetails(data) : formatClusterDetails(data);
  return formattedData;
};

export const getAnomalyMetricMeasure = (anomalyData: Anomaly) => {
  let metricMeasure = MetricMeasure.OVERALL;
  if (
    anomalyData?.defaultSettings?.splitMode === SplitType.NONE &&
    anomalyData?.defaultSettings?.splitType === SplitType.NONE
  ) {
    metricMeasure = MetricMeasure.OVERALL;
  }

  if (
    anomalyData?.defaultSettings?.splitMode === SplitMode.TOP ||
    anomalyData?.defaultSettings?.splitMode === SplitMode.BOTTOM
  ) {
    metricMeasure = anomalyData?.defaultSettings?.splitType === SplitType.NODE ? MetricMeasure.OUTLIER : MetricMeasure.OUTLIER_TABLES;
  } 

  return metricMeasure;
};

export const getAnomalyOutlierType = (anomalyData: Anomaly) => {
  let outlierType: SplitMode = SplitMode.TOP;
  if (anomalyData?.defaultSettings?.splitMode === SplitMode.TOP) {
    outlierType = SplitMode.TOP;
  } else if (anomalyData?.defaultSettings?.splitMode === SplitMode.BOTTOM) {
    outlierType = SplitMode.BOTTOM;
  }

  return outlierType;
};

export const getAnomalyNumNodes = (anomalyData: Anomaly) => {
  let numNodes = MIN_OUTLIER_NUM_NODES;
  if (anomalyData?.defaultSettings?.splitCount > 0) {
    numNodes = anomalyData?.defaultSettings?.splitCount;
  }
  return numNodes;
};

export const getAnomalyStartDate = (anomalyData: Anomaly) => {
  return anomalyData?.graphStartTime ? new Date(anomalyData.graphStartTime!) : null;
};

export const getAnomalyEndTime = (anomalyData: Anomaly) => {
   return anomalyData?.graphEndTime ? new Date(anomalyData.graphEndTime!) : null;
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
};

export const getGraphRequestParams = (anomalyData: Anomaly, startDate=null, endDate=null, splitType=null, splitMode=null, splitNum=null) => {
  const mainGraphRequest = anomalyData?.mainGraphs.map((graph: GraphMetadata) => {
      const request: any = {};
      request.name = graph.name;
      request.filters = graph.filters;
      request.start = anomalyData.graphStartTime;
      request.end = anomalyData.graphEndTime;
      request.settings = anomalyData.defaultSettings;
       // if (graph.name === "active_session_history") {
      //   request.groupBy = [];
      // }
      if (graph.name.startsWith('active_session_history')) {
        request.groupBy = [];
      }
      return request;
  });
  
  const nestedRequest = anomalyData?.rcaGuidelines?.map((rca: RCAGuideline) => {
    return rca.troubleshootingRecommendations?.map((recommendation: TroubleshootingRecommendations) => {
      return recommendation.supportingGraphs?.map((graph: GraphMetadata) => {
      const request: any = {};
      request.name = graph.name;
      request.filters = graph.filters;
      request.start = anomalyData.graphStartTime;
      request.end = anomalyData.graphEndTime;
      request.settings = anomalyData.defaultSettings;
      // if (graph.name === "active_session_history") {
      //   request.groupBy = [];
      // }
      if (graph.name.startsWith('active_session_history')) {
        request.groupBy = [];
      }
      return request;
      })
    })
  });

  const flattenedRequest = nestedRequest?.flat(2);
  const supportingGraphRequest = flattenedRequest?.filter((request) => isNonEmptyObject(request));

  const requestParamsList = [...mainGraphRequest, ...supportingGraphRequest];
  return requestParamsList;
};

export const getRecommendationMetricsMap = (anomalyData: Anomaly) => {
  if (!anomalyData) {
    return [];
  }

   const nestedRecommendationMetrics = anomalyData?.rcaGuidelines?.map((rca: RCAGuideline) => {
      const params: any = {
        cause: '',
        name: [],
        description: ''
      };
      params.cause = rca.possibleCause;
      params.description = rca.possibleCauseDescription
    return rca.troubleshootingRecommendations?.map((recommendation: TroubleshootingRecommendations) => {
      if (!recommendation.supportingGraphs) {
        return params;
      }
      return recommendation.supportingGraphs?.map((graph: GraphMetadata) => {
        params.name.push(graph.name);
        return params;
      })
    })
  });

  const flattenedRecommendationMetrics = nestedRecommendationMetrics?.flat(2);
  const recommendationMetricsMap = new Set(flattenedRecommendationMetrics);
  const recommendationMetrics = Array.from(recommendationMetricsMap);
  
  return recommendationMetrics;
};

