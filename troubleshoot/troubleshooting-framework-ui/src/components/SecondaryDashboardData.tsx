import { useEffect, useState, ChangeEvent } from 'react';
import { usePrevious } from 'react-use';
import { useQuery } from 'react-query';
import { Box, Typography } from '@material-ui/core';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import _ from 'lodash';
import clsx from 'clsx';
import { YBPanelItem } from '../common/YBPanelItem';
import { SecondaryDashboardHeader } from './SecondaryDashboardHeader';
import { SecondaryDashboard } from './SecondaryDashboard';
import { CPU_USAGE_OUTLIER_DATA, SQL_QUERY_LATENCY_OUTLIER_DATA } from './GraphMockOutlierData';
import { CPU_USAGE_OVERALL_DATA, SQL_QUERY_LATENCY_OVERALL_DATA } from './GraphMockOverallData';
import { QUERY_KEY, TroubleshootAPI } from '../api';
import {
  Anomaly,
  AnomalyCategory,
  AppName,
  GraphQuery,
  GraphResponse,
  MetricMeasure,
  Universe
} from '../helpers/dtos';
import { isNonEmptyArray, isNonEmptyObject } from '../helpers/ObjectUtils';
import {
  getAnomalyMetricMeasure,
  getAnomalyOutlierType,
  getAnomalyNumNodes,
  getAnomalyStartDate,
  getAnomalyEndTime,
  getGraphRequestParams
} from '../helpers/utils';
import {
  ALL,
  filterDurations,
  MAX_OUTLIER_NUM_NODES,
  ALL_REGIONS,
  ALL_ZONES
} from '../helpers/constants';
import { useStyles } from './styles';

export interface SecondaryDashboardDataProps {
  anomalyData: Anomaly | undefined;
  universeUuid: string;
  universeData: Universe | any;
  appName: AppName;
  timezone?: string;
}

export const SecondaryDashboardData = ({
  universeUuid,
  universeData,
  anomalyData,
  appName,
  timezone
}: SecondaryDashboardDataProps) => {
  const classes = useStyles();

  // Get default values to be populated on page
  const anomalyMetricMeasure = getAnomalyMetricMeasure(anomalyData!);
  const anomalyOutlierType = getAnomalyOutlierType(anomalyData!);
  const anomalyOutlierNumNodes = getAnomalyNumNodes(anomalyData!);
  const anomalyStartDate = getAnomalyStartDate(anomalyData!);
  const anomalyEndDate = getAnomalyEndTime(anomalyData!);
  const [graphRequestParams, setGraphRequestParams] = useState<GraphQuery[]>(
    getGraphRequestParams(anomalyData!)
  );
  const today = new Date();
  const yesterday = new Date(today);
  yesterday.setDate(yesterday.getDate() - 1);

  // State variables
  const [clusterRegionItem, setClusterRegionItem] = useState<string>(ALL_REGIONS);
  const [zoneNodeItem, setZoneNodeItem] = useState<string>(ALL_ZONES);
  const [isPrimaryCluster, setIsPrimaryCluster] = useState<boolean>(true);
  const [cluster, setCluster] = useState<string>(ALL);
  const [region, setRegion] = useState<string>(ALL);
  const [zone, setZone] = useState<string>(ALL);
  const [node, setNode] = useState<string>(ALL);
  const [metricMeasure, setMetricMeasure] = useState<string>(anomalyMetricMeasure);
  const [outlierType, setOutlierType] = useState<string>(anomalyOutlierType);
  const [filterDuration, setFilterDuration] = useState<string>(
    anomalyStartDate ? filterDurations[filterDurations.length - 1].label : filterDurations[0].label
  );
  const [numNodes, setNumNodes] = useState<number>(anomalyOutlierNumNodes);
  const [startDateTime, setStartDateTime] = useState<Date>(anomalyStartDate ?? yesterday);
  const [endDateTime, setEndDateTime] = useState<Date>(anomalyEndDate ?? today);
  const [chartData, setChartData] = useState<any>(null);
  const [graphData, setGraphData] = useState<any>(null);
  // Check previous props
  const previousMetricMeasure = usePrevious(metricMeasure);

  // Make use of useMutation to call fetchGraphs and onSuccess of it, ensure to set setChartData

  useEffect(() => {
    if (previousMetricMeasure && previousMetricMeasure !== metricMeasure) {
      setChartData(null);
    }
    if (previousMetricMeasure && metricMeasure === MetricMeasure.OUTLIER) {
      if (anomalyData?.category === AnomalyCategory.NODE) setChartData(CPU_USAGE_OUTLIER_DATA);
      if (anomalyData?.category === AnomalyCategory.SQL)
        setChartData(SQL_QUERY_LATENCY_OUTLIER_DATA);
    }
    if (previousMetricMeasure && metricMeasure === MetricMeasure.OVERALL) {
      if (anomalyData?.category === AnomalyCategory.NODE) {
        setChartData(CPU_USAGE_OVERALL_DATA);
      }
      if (anomalyData?.category === AnomalyCategory.SQL)
        setChartData(SQL_QUERY_LATENCY_OVERALL_DATA);
    }
  }, [numNodes, metricMeasure, filterDuration, outlierType, node, zone, region, cluster]);

  const { isLoading, isError, isIdle } = useQuery(
    [QUERY_KEY.fetchGraphs, universeUuid],
    () => TroubleshootAPI.fetchGraphs(universeUuid, graphRequestParams),
    {
      enabled: isNonEmptyObject(anomalyData),
      onSuccess: (graphData: GraphResponse[]) => {
        setGraphData(graphData);
      },
      onError: (error: any) => {
        console.error('Failed to fetch graphs', error);
      }
    }
  );

  useEffect(() => {
    // TODO: Call the useMutation API call during onMount
    // TODO: Then pass the response to MetricsPanel
    let data = null;
    if (anomalyData?.category === AnomalyCategory.NODE) {
      data = CPU_USAGE_OUTLIER_DATA;
    }

    if (anomalyData?.category === AnomalyCategory.SQL) {
      data = SQL_QUERY_LATENCY_OVERALL_DATA;
    }

    setChartData(data);
  }, []);

  const onSplitTypeSelected = (metricMeasure: string) => {
    setMetricMeasure(metricMeasure);
  };

  const onOutlierTypeSelected = (outlierType: string) => {
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
        {row.troubleshootingRecommendations.map((recommendation: string, idx: number) => (
          // eslint-disable-next-line react/no-array-index-key
          <Box key={idx} mt={idx > 0 ? 2 : 0}>
            <Typography variant="body2">
              <li>{recommendation}</li>
            </Typography>
          </Box>
        ))}
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
        setRegion(ALL);
      }

      if (isRegion) {
        setRegion(selectedOption);
        setCluster(ALL);
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
      isZone ? setZone(selectedOption) : setNode(selectedOption);
    }
  };

  const onStartDateChange = (e: ChangeEvent<HTMLInputElement>) => {
    setStartDateTime(new Date(e.target.value));
  };

  const onEndDateChange = (e: ChangeEvent<HTMLInputElement>) => {
    setEndDateTime(new Date(e.target.value));
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
            <Box ml={0.5} className={classes.troubleshootBox}>
              <span className={clsx(classes.troubleshootTitle, classes.tagGreen)}>
                {`${anomalyData?.category} ISSUE`}
              </span>
              <span>
                <b>{anomalyData?.summary}</b>
              </span>
            </Box>
            {/* <Box m={3} id="nodeIssueGraph"></Box> */}
            {chartData &&
              chartData?.length > 0 &&
              chartData?.map((metricData: any) => {
                let uniqueOperations: any = new Set();

                if (metricMeasure === MetricMeasure.OUTLIER && isNonEmptyObject(metricData)) {
                  metricData.data.forEach((metricItem: any) => {
                    uniqueOperations.add(metricItem.name);
                  });
                }
                uniqueOperations = Array.from(uniqueOperations);
                return (
                  <Box mt={2}>
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
                    />
                  </Box>
                );
              })}
            {anomalyData && (
              <Box mt={6} ml={1} mr={7} mb={4}>
                <Typography variant="h2" style={{ marginBottom: '16px' }}>
                  {'Recommendations'}
                </Typography>
                <BootstrapTable
                  data={anomalyData.rcaGuidelines}
                  pagination={anomalyData.rcaGuidelines.length > 10}
                  containerClass={classes.guidelineBox}
                >
                  <TableHeaderColumn dataField="index" isKey={true} hidden={true} />
                  <TableHeaderColumn
                    width="25%"
                    tdStyle={{ whiteSpace: 'normal', verticalAlign: 'middle' }}
                    thStyle={{ textAlign: 'center', fontWeight: 600 }}
                    dataField="possibleCause"
                    // dataFormat={formatDisplayName}
                    dataSort
                  >
                    <span>{'Possible Cause'}</span>
                  </TableHeaderColumn>
                  <TableHeaderColumn
                    width="45%"
                    tdStyle={{ whiteSpace: 'normal', verticalAlign: 'middle' }}
                    thStyle={{ textAlign: 'center', fontWeight: 600 }}
                    // dataFormat={formatDisplayName}
                    dataField="possibleCauseDescription"
                    dataSort
                  >
                    <span>{'Description'}</span>
                  </TableHeaderColumn>
                  <TableHeaderColumn
                    width="30%"
                    thStyle={{ textAlign: 'center', fontWeight: 600 }}
                    dataFormat={formatRecommendations}
                    dataSort
                  >
                    <span>{'Recommendations'}</span>
                  </TableHeaderColumn>
                </BootstrapTable>
              </Box>
            )}
          </>
        }
      />
    </Box>
  );
};
