import React, { FC, useState, useEffect, useMemo } from 'react';
import { Box, Grid, makeStyles, MenuItem, LinearProgress } from '@material-ui/core';
// import { useLocalStorage } from 'react-use';
import { useTranslation } from 'react-i18next';
import clsx from 'clsx';
import { ArrayParam, StringParam, useQueryParams, withDefault } from 'use-query-params';

// Local imports
import { RelativeInterval, countryToFlag, getRegionCode } from '@app/helpers';
import { ItemTypes } from '@app/helpers/dnd/types';
import { YBButton, YBSelect, YBDragableAndDropable, YBDragableAndDropableItem } from '@app/components';
import {
  ClusterRegionInfo,
  useGetClusterNodesQuery,
  useGetClusterQuery
} from '@app/api/src';
// import {
//   MetricsOptionsModal,
//   MetricsQueryParams
// } from '@app/features/clusters/details/performance/metrics/MetricsOptionsModal';
import { ChartController } from '@app/features/clusters/details/overview/ChartControler';
import { useChartConfig } from '@app/features/clusters/details/overview/ChartConfig';
// import { PerformanceClusterInfo } from './PerformanceClusterInfo';
// import type { ClusterResponse } from '@app/api/src/models/ClusterResponse';

// Icons
// import EditIcon from '@app/assets/edit.svg';
import RefreshIcon from '@app/assets/refresh.svg';
import { VCpuUsagePanel } from './VCpuUsagePanel';

const useStyles = makeStyles((theme) => ({
  divider: {
    marginBottom: theme.spacing(2)
  },
  chartContainer: {
    display: 'grid',
    gridTemplateColumns: 'repeat(2, 1fr)',
    gridColumnGap: theme.spacing(1.25),
    gridRowGap: theme.spacing(1.25)
  },
  singleColumn: {
    gridTemplateColumns: 'repeat(1, 1fr)'
  },
  selectBox: {
    width: '180px',
    marginRight: theme.spacing(1)
  }
}));

const defaultVisibleGraphList =
  ['operations', 'latency', 'cpuUsage', 'diskUsage', 'totalLiveNodes'];

export const Metrics: FC = () => {
  const { t } = useTranslation();
  const classes = useStyles();
  const chartConfig = useChartConfig();
  // since we assume only one cluster, we hard code the key instead of using the cluster id
  // const [savedCharts, setSavedCharts] =
  //   useLocalStorage<string[]>("defaultClusterId", defaultVisibleGraphList);
  const [savedCharts, setSavedCharts] = useState(defaultVisibleGraphList);
  const displayedCharts = savedCharts?.filter((chart) => chartConfig[chart] !== undefined);
  const [refresh, doRefresh] = useState(0);

  // Check if feature flag enabled
  // const { data: runtimeConfig, isLoading: runtimeConfigLoading } = useRuntimeConfig();
  // const { data: runtimeConfigAccount, isLoading: runtimeConfigAccountLoading } =
  // useRuntimeConfig(
  //   params.accountId ?? ''
  // );

  // const isMultiRegionEnabled =
  //   runtimeConfig &&
  //   (runtimeConfig?.MultiRegionEnabled || runtimeConfigAccount?.MultiRegionEnabled);

  const isMultiRegionEnabled = false;

  const { data: clusterData } = useGetClusterQuery();

  const ALL_REGIONS = { label: t('clusterDetail.overview.allRegions'), value: '' };
  const ALL_NODES = { label: t('clusterDetail.performance.metrics.allNodes'), value: 'all' };
  const [queryParams, setQueryParams] = useQueryParams({
    showGraph: withDefault(ArrayParam, displayedCharts),
    nodeName: withDefault(StringParam, ALL_NODES.value),
    interval: withDefault(StringParam, RelativeInterval.LastHour),
    region: withDefault(StringParam, ALL_REGIONS.value)
  });

  // const [ setIsMetricsOptionsModalOpen] = useState<boolean>(false);
  const [nodeName, setNodeName] = useState<string | undefined>(queryParams.nodeName);
  const [relativeInterval, setRelativeInterval] = useState<string>(queryParams.interval);

  const [region, setRegion] = React.useState<string>(queryParams.region || '');
  const [selectedRegion, selectedZone] = region ? region.split('#') : ['', ''];

  const { data: nodesResponse, isLoading: isClusterNodesLoading } = useGetClusterNodesQuery();

  const nodesNamesList = useMemo(
    () => [
      ALL_NODES,
      ...(nodesResponse?.data
        .filter(
          (node) =>
            (selectedRegion === "" && selectedZone === "") ||
            (node.cloud_info.region === selectedRegion && node.cloud_info.zone === selectedZone)
        )
        .map((node) => ({ label: node.name, value: node.name })) ?? []),
    ],
    [nodesResponse?.data, selectedRegion, selectedZone]
  );

  const handleSetDndOrderedCharts = (newDisplayedChart: string[]) => {
    setSavedCharts(newDisplayedChart);
  };

  const handleChangeFilterOrChangeDisplayChart = (
    newInterval: string,
    newNodeName: string | undefined,
    newChartList: string[],
    newRegion: string,
  ) => {
    setRelativeInterval(newInterval);
    setNodeName(newNodeName);
    setSavedCharts(newChartList);
    setRegion(newRegion);
    setQueryParams({
      interval: newInterval,
      nodeName: newNodeName,
      showGraph: newChartList,
      region: newRegion,
    });
  };

  useEffect(() => {
    handleChangeFilterOrChangeDisplayChart(
      relativeInterval,
      queryParams.nodeName,
      displayedCharts ?? [],
      queryParams.region
    );
  }, [queryParams.nodeName])

  useEffect(() => {
    if (nodeName && !nodesNamesList.find((node) => node.value === nodeName)) {
      handleChangeFilterOrChangeDisplayChart(
        relativeInterval,
        ALL_NODES.value,
        displayedCharts ?? [],
        queryParams.region
      );
    }
  }, [region])

  useEffect(() => {
    doRefresh((prev) => prev + 1);
  }, [savedCharts]);

  if (isClusterNodesLoading) {// || runtimeConfigLoading || runtimeConfigAccountLoading) {
    return <LinearProgress />;
  }

  // const regionsList: ClusterRegionInfo[] = clusterData?.data?.spec?.cluster_region_info ?? [];
  const regionsList: ClusterRegionInfo[] = [];
  const regionsNamesList = [
    ALL_REGIONS,
    ...(regionsList.map((region) => ({ label: region.placement_info.cloud_info.region, value: region.placement_info.cloud_info.region })) ?? [])
  ];

  const regionData = useMemo(() => {
    const set = new Set<string>();
    nodesResponse?.data.forEach(node => set.add(node.cloud_info.region + "#" + node.cloud_info.zone));
    return Array.from(set).map(regionZone => {
      const [region, zone] = regionZone.split('#');
      return {
        region,
        zone,
        flag: countryToFlag(getRegionCode({ region, zone })),
      }
    });
  }, [nodesResponse]);

  // const onRegionChange = (region: string) => {
  //   setSelectedRegion(region);
  // };

  return (
    <>
      {/*<MetricsOptionsModal
        metricsQueryParams={queryParams as MetricsQueryParams}
        visibleGraphList={displayedCharts ?? []}
        onChange={setSavedCharts}
        nodeName={ALL_NODES.value}
        open={isMetricsOptionsModalOpen}
        setVisibility={setIsMetricsOptionsModalOpen}
  />*/}
      {clusterData?.data &&
        <VCpuUsagePanel cluster={clusterData.data} />
      }
      <Grid container justifyContent="space-between" alignItems="center">
        <Grid item xs={6}>
          <Box display="flex">
            {isMultiRegionEnabled && (
              <Box mr={1}>
                {/*<RegionSelector
                  cloud={clusterData?.data?.spec?.cloud_info?.code ?? ''}
                  availableRegions={regionsList}
                  onChange={onRegionChange}
            />*/}
                <YBSelect
                  className={classes.selectBox}
                  value={region}
                  onChange={(e) => {
                    setRegion((e.target as HTMLInputElement).value);
                  }}
                >
                  {regionsNamesList?.map((el) => {
                    return (
                      <MenuItem key={el.label} value={el.value}>
                        {el.label}
                      </MenuItem>
                    )
                  })}
                </YBSelect>
              </Box>
            )}
            <YBSelect
              className={classes.selectBox}
              value={region}
              onChange={(e) => {
                handleChangeFilterOrChangeDisplayChart(
                  relativeInterval,
                  nodeName,
                  displayedCharts ?? [],
                  (e.target as HTMLInputElement).value
                )
              }}
            >
              <MenuItem value={''}>
                {t('clusterDetail.performance.metrics.allRegions')}
              </MenuItem>
              {/* <Divider className={classes.dropdownDivider} /> */}
              {regionData.map(data => (
                <MenuItem key={data.region + '#' + data.zone} value={data.region + '#' + data.zone}>
                  {data.flag && <Box mr={1}>{data.flag}</Box>} {data.region} ({data.zone})
                </MenuItem>
              ))}
            </YBSelect>
            <YBSelect
              className={classes.selectBox}
              value={nodeName}
              onChange={(e) => {
                handleChangeFilterOrChangeDisplayChart(
                  relativeInterval,
                  (e.target as HTMLInputElement).value,
                  displayedCharts ?? [],
                  region
                );
              }}
            >
              {nodesNamesList?.map((el) => {
                return (
                  <MenuItem key={el.label} value={el.value}>
                    {el.label}
                  </MenuItem>
                );
              })}
            </YBSelect>
            <YBSelect
              className={classes.selectBox}
              value={relativeInterval}
              onChange={(e) => {
                handleChangeFilterOrChangeDisplayChart(
                  (e.target as HTMLInputElement).value,
                  nodeName,
                  displayedCharts ?? [],
                  region
                );
              }}
            >
              <MenuItem value={RelativeInterval.LastHour}>{t('clusterDetail.lastHour')}</MenuItem>
              <MenuItem value={RelativeInterval.Last6Hours}>{t('clusterDetail.last6hours')}</MenuItem>
              <MenuItem value={RelativeInterval.Last12hours}>{t('clusterDetail.last12hours')}</MenuItem>
              <MenuItem value={RelativeInterval.Last24hours}>{t('clusterDetail.last24hours')}</MenuItem>
              {/*<MenuItem value={RelativeInterval.Last7days}>{t('clusterDetail.last7days')}</MenuItem>*/}
            </YBSelect>
          </Box>
        </Grid>
        <Grid item container xs={6} justify="flex-end">
          <Grid item>
            <Box mr={2}>
              <YBButton variant="ghost" startIcon={<RefreshIcon />} onClick={() => doRefresh((prev) => prev + 1)}>
                {t('clusterDetail.performance.actions.refreshCharts')}
              </YBButton>
            </Box>
          </Grid>
          {/*<Grid item>
            <YBButton variant="ghost" startIcon={<EditIcon />} onClick={() => setIsMetricsOptionsModalOpen(true)}>
              {t('clusterDetail.performance.actions.options')}
            </YBButton>
            </Grid>*/}
        </Grid>
        {isMultiRegionEnabled && (
          <Grid item xs={12}>
            <Box mt={2}>
              {/* <PerformanceClusterInfo
                cluster={clusterData?.data}
                region={selectedRegion}
                availableRegions={[]}
        />*/}
            </Box>
          </Grid>
        )}
      </Grid>
      <div className={classes.divider} />
      <Grid container spacing={2}>
        <Grid item xs={12}>
          <YBDragableAndDropable
            className={clsx(classes.chartContainer, displayedCharts?.length === 1 ? classes.singleColumn : '')}
          >
            {displayedCharts?.map((chartName, index) => {
              const config = chartConfig[chartName];
              return (
                <YBDragableAndDropableItem
                  key={chartName}
                  index={index}
                  id={chartName}
                  type={ItemTypes.card}
                  data={displayedCharts}
                  onChange={handleSetDndOrderedCharts}
                >
                  <ChartController
                    nodeName={nodeName}
                    title={config.title}
                    metric={config.metric}
                    unitKey={config.unitKey}
                    metricChartLabels={config.chartLabels}
                    strokes={config.strokes}
                    chartDrawingType={config.chartDrawingType}
                    relativeInterval={relativeInterval as RelativeInterval}
                    refreshFromParent={refresh}
                    regionName={selectedRegion}
                    zone={selectedZone}
                  />
                </YBDragableAndDropableItem>
              );
            })}
          </YBDragableAndDropable>
        </Grid>
      </Grid>
    </>
  );
};
