import React, { FC, useState, useEffect, useMemo } from 'react';
import { Box, Grid, makeStyles, MenuItem, LinearProgress, Typography } from '@material-ui/core';
// import { useLocalStorage } from 'react-use';
import { useTranslation } from 'react-i18next';
import clsx from 'clsx';
import { ArrayParam, StringParam, useQueryParams, withDefault } from 'use-query-params';

// Local imports
import { ClusterType, countryToFlag, getRegionCode, RelativeInterval } from '@app/helpers';
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
    minWidth: '200px',
    marginRight: theme.spacing(1)
  },
  clusterButton: {
    borderRadius: theme.shape.borderRadius,
    marginRight: theme.spacing(1),

    '&:hover': {
      borderColor: theme.palette.grey[300]
    }
  },
  tablesRow: {
    display: 'flex',
    alignItems: 'center',
    margin: theme.spacing(2, 0, 2.5, 0)
  },
  selected: {
    backgroundColor: theme.palette.grey[300],

    '&:hover': {
      backgroundColor: theme.palette.grey[300]
    }
  },
  buttonText: {
    color: theme.palette.text.primary
  },
  dropdownTitle: {
    margin: theme.spacing(0.5, 1.5),
    fontWeight: 400,
  },
  dropdownDivider: {
    margin: theme.spacing(1, 0, 1.5, 0),
  },
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
    region: withDefault(StringParam, ALL_REGIONS.value),
    clusterType: withDefault(StringParam, 'PRIMARY')
  });

  const [tab, setTab] = React.useState<ClusterType>(queryParams.clusterType as ClusterType);
  const [region, setRegion] = React.useState<string>(queryParams.region || '');
  const [selectedRegion, selectedZone] = region ? region.split('#') : ['', ''];

  // const [ setIsMetricsOptionsModalOpen] = useState<boolean>(false);
  const [nodeName, setNodeName] = useState<string | undefined>(queryParams.nodeName);
  const [relativeInterval, setRelativeInterval] = useState<string>(queryParams.interval);

  const { data: nodesResponse, isLoading: isClusterNodesLoading } = useGetClusterNodesQuery({});
  const hasReadReplica = !!nodesResponse?.data.find((node) => node.is_read_replica);

  const nodesNamesList = useMemo(
    () => [
      ALL_NODES,
      ...(nodesResponse?.data
        .filter(
          (node) =>
            (tab === "PRIMARY" && !node.is_read_replica) ||
            (tab === "READ_REPLICA" && node.is_read_replica)
        )
        .filter(
          (node) =>
            (selectedRegion === "" && selectedZone === "") ||
            (node.cloud_info.region === selectedRegion && node.cloud_info.zone === selectedZone)
        )
        .map((node) => ({ label: node.name, value: node.name })) ?? []),
    ],
    [tab, nodesResponse?.data, selectedRegion, selectedZone]
  );

  function handleTabChange(newTab: typeof tab) {
    setTab(newTab);
    setNodeName(ALL_NODES.value);
    setRelativeInterval(RelativeInterval.LastHour);
    setRegion(ALL_REGIONS.value);
    setQueryParams({
      interval: RelativeInterval.LastHour,
      nodeName: ALL_NODES.value,
      showGraph: savedCharts,
      region: ALL_REGIONS.value,
      clusterType: newTab
    });
  }

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
      clusterType: tab
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
    if (queryParams.region) {
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

  // const regionsList: ClusterRegionInfo[] = clusterData?.data?.spec?.cluster_region_info ?? [];
  const regionsList: ClusterRegionInfo[] = [];
  const regionsNamesList = [
    ALL_REGIONS,
    ...(regionsList.map((region) => ({ label: region.placement_info.cloud_info.region, value: region.placement_info.cloud_info.region })) ?? [])
  ];

  const regionData = useMemo(() => {
    const set = new Set<string>();
    nodesResponse?.data.filter(node => (tab === "PRIMARY" && !node.is_read_replica) ||
      (tab === "READ_REPLICA" && node.is_read_replica))
      .forEach(node => set.add(node.cloud_info.region + "#" + node.cloud_info.zone));
    return Array.from(set).map(regionZone => {
      const [region, zone] = regionZone.split('#');
      return {
        region,
        zone,
        flag: countryToFlag(getRegionCode({ region, zone })),
      }
    });
  }, [nodesResponse, tab]);

  if (isClusterNodesLoading) {// || runtimeConfigLoading || runtimeConfigAccountLoading) {
    return <LinearProgress />;
  }

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
    <Box className={classes.tablesRow}>
      {hasReadReplica &&
        <>
          <YBButton
            className={
                clsx(classes.clusterButton, tab === 'PRIMARY' && classes.selected)
            }
            onClick={() => handleTabChange('PRIMARY')}
          >
            <Typography variant="body2" className={classes.buttonText}>
              {t('clusterDetail.performance.metrics.primaryCluster')}
            </Typography>
          </YBButton>
          <YBButton
            className={
                clsx(classes.clusterButton, tab === 'READ_REPLICA' && classes.selected)
            }
            onClick={() => handleTabChange('READ_REPLICA')}
          >
            <Typography variant="body2" className={classes.buttonText}>
            {t('clusterDetail.performance.metrics.readReplicas')}
            </Typography>
          </YBButton>
        </>
      }
      </Box>
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
                  ALL_NODES.value,
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
                    clusterType={tab}
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
