import { useEffect, useState, ChangeEvent } from 'react';
import { usePrevious } from 'react-use';
import { useMutation } from 'react-query';
import { useUpdateEffect } from 'react-use';
import { Box, Typography } from '@material-ui/core';
import _ from 'lodash';
import clsx from 'clsx';
import { YBPanelItem } from '../common/YBPanelItem';
import { SecondaryDashboardHeader } from './SecondaryDashboardHeader';
import { SecondaryDashboard } from './SecondaryDashboard';
import { TroubleshootAPI } from '../api';
import {
  Anomaly,
  AppName,
  GraphQuery,
  GraphResponse,
  GraphSettings,
  GraphType,
  MetricMeasure,
  SplitMode,
  SplitType,
  TroubleshootingRecommendations,
  Universe
} from '../helpers/dtos';
import {
  isEmptyArray,
  isNonEmptyArray,
  isNonEmptyObject,
  isNonEmptyString
} from '../helpers/objectUtils';
import {
  getAnomalyMetricMeasure,
  getAnomalyOutlierType,
  getAnomalyNumNodes,
  getAnomalyStartDate,
  getAnomalyEndTime
} from '../helpers/utils';
import {
  ALL,
  filterDurations,
  MAX_OUTLIER_NUM_NODES,
  ALL_REGIONS,
  ALL_ZONES
} from '../helpers/constants';
import { useHelperStyles } from './styles';

import TraingleDownIcon from '../assets/traingle-down.svg';
import TraingleUpIcon from '../assets/traingle-up.svg';
import { YBTimeFormats, formatDatetime } from '../helpers/dateUtils';

export interface SecondaryDashboardDataProps {
  anomalyData: Anomaly | null;
  universeUuid: string;
  universeData: Universe | any;
  appName: AppName;
  graphParams: GraphQuery[] | null;
  timezone?: string;
  recommendationMetrics: any;
}

export const SecondaryDashboardData = ({
  universeUuid,
  universeData,
  anomalyData,
  graphParams,
  appName,
  timezone,
  recommendationMetrics
}: SecondaryDashboardDataProps) => {
  const classes = useHelperStyles();
  // Get default values to be populated on page
  const anomalyMetricMeasure = getAnomalyMetricMeasure(anomalyData!);
  const anomalyOutlierType = getAnomalyOutlierType(anomalyData!);
  const anomalyOutlierNumNodes = getAnomalyNumNodes(anomalyData!);
  const anomalyStartDate = getAnomalyStartDate(anomalyData!);
  const anomalyEndDate = getAnomalyEndTime(anomalyData!);
  const today = new Date();
  const yesterday = new Date(today);
  yesterday.setDate(yesterday.getDate() - 1);

  // State variables
  const [openTiles, setOpenTiles] = useState<string[]>([]);
  const [clusterRegionItem, setClusterRegionItem] = useState<string>(ALL_REGIONS);
  const [zoneNodeItem, setZoneNodeItem] = useState<string>(ALL_ZONES);
  const [isPrimaryCluster, setIsPrimaryCluster] = useState<boolean>(true);
  const [cluster, setCluster] = useState<string>(ALL);
  const [region, setRegion] = useState<string>(ALL);
  const [zone, setZone] = useState<string>(ALL);
  const [node, setNode] = useState<string>(ALL);
  const [metricMeasure, setMetricMeasure] = useState<string>(anomalyMetricMeasure);
  const [outlierType, setOutlierType] = useState<SplitMode>(anomalyOutlierType);
  const [filterDuration, setFilterDuration] = useState<string>(
    anomalyStartDate ? filterDurations[filterDurations.length - 1].label : filterDurations[0].label
  );
  const [numNodes, setNumNodes] = useState<number>(anomalyOutlierNumNodes);
  const [startDateTime, setStartDateTime] = useState<Date>(anomalyStartDate ?? yesterday);
  const [endDateTime, setEndDateTime] = useState<Date>(anomalyEndDate ?? today);
  const [graphData, setGraphData] = useState<any>(null);
  // Check previous props
  const previousMetricMeasure = usePrevious(metricMeasure);

  // Make use of useMutation to call fetchGraphs and onSuccess of it, ensure to set setChartData
  useUpdateEffect(() => {
    if (previousMetricMeasure && previousMetricMeasure !== metricMeasure) {
      setGraphData(null);
    }

    const graphParamsCopy = JSON.parse(JSON.stringify(graphParams));

    const formattedStartDate = startDateTime?.toISOString();
    const formattedEndDate = endDateTime?.toISOString();

    graphParamsCopy?.map((params: GraphQuery) => {
      const settings = params.settings;
      const filters = params.filters;
      let start = params.start;
      let end = params.end;

      if (start !== formattedStartDate) {
        params.start = formattedStartDate;
      }

      if (end !== formattedEndDate) {
        params.end = formattedEndDate;
      }
      if (isNonEmptyString(cluster) && cluster !== ALL) {
        filters.clusterUuid = [cluster];
      }
      if (isNonEmptyString(region) && region !== ALL) {
        filters.regionCode = [region];
      }
      if (isNonEmptyString(zone) && zone !== ALL) {
        filters.azCode = [zone];
      }
      if (isNonEmptyString(node) && node !== ALL) {
        filters.instanceName = [node];
      }
      settings.returnAggregatedValue = metricMeasure !== MetricMeasure.OVERALL;
      settings.splitType =
        metricMeasure === MetricMeasure.OVERALL
          ? SplitType.NONE
          : metricMeasure === MetricMeasure.OUTLIER
          ? SplitType.NODE
          : SplitType.TABLE;
      settings.splitMode = metricMeasure === MetricMeasure.OVERALL ? SplitMode.NONE : outlierType;
      settings.splitCount = numNodes;
      return settings;
    });

    const actualQueryParams = graphParamsCopy ?? graphParams;
    fetchAnomalyGraphs.mutate(actualQueryParams);
  }, [
    numNodes,
    metricMeasure,
    filterDuration,
    outlierType,
    node,
    zone,
    region,
    cluster,
    endDateTime,
    startDateTime,
    anomalyData?.graphStartTime,
    anomalyData?.graphEndTime
  ]);

  const fetchAnomalyGraphs = useMutation(
    (params: any) => TroubleshootAPI.fetchGraphs(universeUuid, params),
    {
      onSuccess: (graphData: GraphResponse[]) => {
        setGraphData(graphData);
      },
      onError: (error: any) => {
        console.error('Failed to fetch graphs', error);
      }
    }
  );

  useEffect(() => {
    fetchAnomalyGraphs.mutate(graphParams);
  }, []);

  const onSplitTypeSelected = (metricMeasure: string) => {
    setMetricMeasure(metricMeasure);
  };

  const onOutlierTypeSelected = (outlierType: SplitMode) => {
    setOutlierType(outlierType);
  };

  const onNumNodesChanged = (numNodes: number) => {
    setNumNodes(numNodes > MAX_OUTLIER_NUM_NODES ? MAX_OUTLIER_NUM_NODES : numNodes);
  };

  const onSelectedFilterDuration = (filterDuration: string) => {
    setFilterDuration(filterDuration);
  };

  const formatRecommendations = (cell: any, row: any) => {
    return (
      <Box>
        {row.troubleshootingRecommendations?.map(
          (recommendation: TroubleshootingRecommendations, idx: number) => (
            // eslint-disable-next-line react/no-array-index-key
            <Box key={idx} mt={idx > 0 ? 2 : 0}>
              <Typography variant="body2">
                <li>{recommendation.recommendation}</li>
              </Typography>
            </Box>
          )
        )}
      </Box>
    );
  };

  const onClusterRegionSelected = (
    isCluster: boolean,
    isRegion: boolean,
    selectedOption: string,
    isPrimaryCluster: boolean
  ) => {
    setIsPrimaryCluster(isPrimaryCluster);
    if (selectedOption === ALL_REGIONS) {
      setClusterRegionItem(ALL_REGIONS);
      setCluster(ALL);
      setRegion(ALL);
    }

    if (isCluster || isRegion) {
      setClusterRegionItem(selectedOption);

      if (isCluster) {
        setCluster(selectedOption);
        setRegion('');
      }

      if (isRegion) {
        setRegion(selectedOption);
        setCluster('');
      }
    }
  };

  const onZoneNodeSelected = (isZone: boolean, isNode: boolean, selectedOption: string) => {
    if (selectedOption === ALL_ZONES) {
      setZoneNodeItem(ALL_ZONES);
      setZone(ALL);
      setNode(ALL);
    }

    if (isZone || isNode) {
      setZoneNodeItem(selectedOption);

      if (isZone) {
        setZone(selectedOption);
        setNode('');
      }

      if (isNode) {
        setZone('');
        setNode(selectedOption);
      }
    }
  };

  const onStartDateChange = (e: ChangeEvent<HTMLInputElement>) => {
    setStartDateTime(new Date(e.target.value));
  };

  const onEndDateChange = (e: ChangeEvent<HTMLInputElement>) => {
    setEndDateTime(new Date(e.target.value));
  };

  const handleOpenBox = (metricName: string) => {
    let openTilesCopy = [...openTiles];

    if (!openTilesCopy.includes(metricName)) {
      openTilesCopy.push(metricName);
      setOpenTiles(openTilesCopy);
    } else if (openTilesCopy.includes(metricName)) {
      const openTileIndex = openTilesCopy.indexOf(metricName);
      if (openTileIndex >= 0) {
        openTilesCopy.splice(openTileIndex, 1);
      }
      setOpenTiles(openTilesCopy);
    }
  };

  const renderSupportingGraphs = (metricData: any, uniqueOperations: any, graphType: GraphType) => {
    return (
      <Box mt={3} mr={8}>
        <SecondaryDashboard
          metricData={metricData}
          metricKey={metricData.name}
          containerWidth={null}
          prometheusQueryEnabled={true}
          metricMeasure={metricMeasure}
          operations={uniqueOperations}
          shouldAbbreviateTraceName={true}
          isMetricLoading={false}
          anomalyData={anomalyData}
          appName={appName}
          timezone={timezone}
          graphType={graphType}
        />
      </Box>
    );
  };

  return (
    <Box>
      <SecondaryDashboardHeader
        appName={appName}
        universeData={universeData}
        clusterRegionItem={clusterRegionItem}
        zoneNodeItem={zoneNodeItem}
        isPrimaryCluster={isPrimaryCluster}
        cluster={cluster}
        region={region}
        zone={zone}
        node={node}
        metricMeasure={metricMeasure}
        outlierType={outlierType}
        filterDuration={filterDuration}
        numNodes={numNodes}
        startDateTime={startDateTime}
        endDateTime={endDateTime}
        anomalyData={anomalyData}
        onZoneNodeSelected={onZoneNodeSelected}
        onClusterRegionSelected={onClusterRegionSelected}
        onOutlierTypeSelected={onOutlierTypeSelected}
        onSplitTypeSelected={onSplitTypeSelected}
        onNumNodesChanged={onNumNodesChanged}
        onSelectedFilterDuration={onSelectedFilterDuration}
        onStartDateChange={onStartDateChange}
        onEndDateChange={onEndDateChange}
        timezone={timezone}
      />
      <YBPanelItem
        body={
          <>
            {isNonEmptyArray(graphData) &&
              isNonEmptyArray(anomalyData?.mainGraphs) &&
              anomalyData?.mainGraphs.map((graph: any, graphIdx: number) => {
                const metricData = graphData.find((data: any) => data.name === graph.name);
                let uniqueOperations: any = new Set();

                if (metricMeasure === MetricMeasure.OUTLIER && isNonEmptyObject(metricData)) {
                  metricData.data.forEach((metricItem: any) => {
                    uniqueOperations.add(metricItem.name);
                  });
                }
                uniqueOperations = Array.from(uniqueOperations);

                return (
                  <Box className={classes.secondaryDashboard}>
                    {graphIdx === 0 && (
                      <Box mt={1} ml={0.5}>
                        <span className={clsx(classes.largeBold)}>
                          {`${anomalyData?.category} Issue: ${anomalyData?.summary} `}
                        </span>
                      </Box>
                    )}
                    {renderSupportingGraphs(metricData, uniqueOperations, GraphType.MAIN)}
                  </Box>
                );
              })}
            {isNonEmptyArray(graphData) &&
              // Display metrics in the same order based on request params tp graph
              // This will help us to group metrics together based on RCA Guidelines
              recommendationMetrics?.map((reason: any, reasonIdx: number) => {
                let renderItems: any = [];
                if (isEmptyArray(reason?.name)) {
                  return (
                    <Box
                      onClick={() => handleOpenBox(reason.cause)}
                      className={classes.secondaryDashboard}
                      key={reason.causee}
                    >
                      <Box>
                        <span className={classes.smallBold}>{reason.cause}</span>
                      </Box>
                      <Box mt={1}>
                        <span className={classes.mediumNormal}>{reason.description}</span>
                      </Box>
                      {openTiles.includes(reason.cause) ? (
                        <img src={TraingleDownIcon} alt="expand" className={classes.arrowIcon} />
                      ) : (
                        <img src={TraingleUpIcon} alt="shrink" className={classes.arrowIcon} />
                      )}
                      {openTiles.includes(reason.cause) && (
                        <Box mt={3}>
                          <span className={classes.smallNormal}>
                            {'THERE ARE NO SUPPORTING METRICS'}
                          </span>
                        </Box>
                      )}
                    </Box>
                  );
                } else {
                  return reason?.name?.map((metricName: string, idx: number) => {
                    let uniqueOperations: any = new Set();
                    const numReasons = reason.name.length - 1;
                    const metricData = graphData.find((data: any) => data.name === metricName);

                    if (metricMeasure === MetricMeasure.OUTLIER && isNonEmptyObject(metricData)) {
                      metricData.data.forEach((metricItem: any) => {
                        uniqueOperations.add(metricItem.name);
                      });
                    }
                    uniqueOperations = Array.from(uniqueOperations);
                    return (
                      <>
                        {idx === 0 && reasonIdx === 0 && (
                          <Box mt={2} mb={2}>
                            <Typography variant="h4">{'Possible Causes'}</Typography>
                          </Box>
                        )}
                        {isNonEmptyObject(metricData) && (
                          <>
                            <Box hidden={true}>
                              {renderItems.push(
                                renderSupportingGraphs(
                                  metricData,
                                  uniqueOperations,
                                  GraphType.SUPPORTING
                                )
                              )}
                            </Box>
                            {idx === numReasons && (
                              <Box
                                onClick={() => handleOpenBox(metricData.name)}
                                className={classes.secondaryDashboard}
                                key={metricData.name}
                              >
                                <Box>
                                  <span className={classes.smallBold}>{reason.cause}</span>
                                </Box>
                                <Box mt={1}>
                                  <span className={classes.mediumNormal}>{reason.description}</span>
                                </Box>
                                {openTiles.includes(metricData.name) ? (
                                  <img
                                    src={TraingleDownIcon}
                                    alt="expand"
                                    className={classes.arrowIcon}
                                  />
                                ) : (
                                  <img
                                    src={TraingleUpIcon}
                                    alt="shrink"
                                    className={classes.arrowIcon}
                                  />
                                )}
                                {openTiles.includes(metricData.name) && (
                                  <Box mt={3}>
                                    <span className={classes.smallNormal}>
                                      {'SUPPORTING METRICS'}
                                    </span>
                                  </Box>
                                )}
                                {openTiles.includes(metricData.name) && (
                                  <Box className={clsx(classes.metricGroupItems)}>
                                    {renderItems?.map((item: any) => {
                                      return item;
                                    })}
                                  </Box>
                                )}
                              </Box>
                            )}
                          </>
                        )}
                      </>
                    );
                  });
                }
              })}
          </>
        }
      />
    </Box>
  );
};
