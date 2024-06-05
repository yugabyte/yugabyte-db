import React, { FC, useMemo } from 'react';
import { Box, Divider, Grid, makeStyles, MenuItem, Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { useHistory, useLocation } from 'react-router-dom';

// Local imports
import { YBSelect } from '@app/components';
// import { YBSelect, RegionSelector } from '@app/components';
import { ClusterInfo } from './ClusterInfo';
import { ClusterInfoWidget } from './ClusterInfoWidget';
import { ClusterStatusWidget } from './ClusterStatusWidget';
import { ClusterType, countryToFlag, getRegionCode, RelativeInterval } from '@app/helpers';
import { useChartConfig } from '@app/features/clusters/details/overview/ChartConfig';
import { ChartController } from '@app/features/clusters/details/overview/ChartControler';
import { useGetClusterHealthCheckQuery, useGetClusterNodesQuery, useGetClusterQuery } from '@app/api/src';


const useStyles = makeStyles((theme) => ({
  metricsRow: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'space-between',
    margin: theme.spacing(2, 0)
  },
  intervalPicker: {
    minWidth: 200
  },
  widgets: {
    display: 'flex',
    flexWrap: 'wrap',
    rowGap: theme.spacing(2)
  },
  dropdownTitle: {
    margin: theme.spacing(0.5, 1.5),
    fontWeight: 400,
  },
  divider: {
    margin: theme.spacing(1, 0, 1.5, 0),
  }
}));

const SEARCH_PARAM_INTERVAL = 'interval';

const chartList = ['operations', 'latency'];
export const MULTI_REGION_FLAG_PATH = 'ybcloud.conf.cluster.geo_partitioning.enabled';

export const OverviewTab: FC = () => {
  const classes = useStyles();
  const { t } = useTranslation();
  const history = useHistory();
  const location = useLocation();
  const chartConfig = useChartConfig();
  const searchParams = new URLSearchParams(location.search);

  // validate interval search param to have the right value
  let interval = searchParams.get(SEARCH_PARAM_INTERVAL) as RelativeInterval;
  if (!Object.values(RelativeInterval).includes(interval)) {
    interval = RelativeInterval.LastHour;
  }

  const changeInterval = (newInterval: string) => {
    const newLocation = { ...location };
    searchParams.set(SEARCH_PARAM_INTERVAL, newInterval);
    newLocation.search = searchParams.toString();
    history.push(newLocation); // this will trigger page re-rendering, thus no need in useState/useEffect
  };

  const { data: nodesResponse } = useGetClusterNodesQuery({});
  const regionData = useMemo(() => {
    const primarySet = new Set<string>();
    const readReplicaSet = new Set<string>();
    nodesResponse?.data.forEach(node => {
      if (!node.is_read_replica) {
        primarySet.add(node.cloud_info.region + "#" + node.cloud_info.zone)
      } else {
        readReplicaSet.add(node.cloud_info.region + "#" + node.cloud_info.zone)
      }
    });

    const getRegionItems = (regionList: string[]) => 
      regionList.map(regionZone => {
        const [region, zone] = regionZone.split('#');
        return {
          region,
          zone,
          flag: countryToFlag(getRegionCode({ region, zone })),
        }
      });
    
    return {
      primary: getRegionItems(Array.from(primarySet)),
      readReplica: getRegionItems(Array.from(readReplicaSet)),
    }
  }, [nodesResponse]);

  const [region, setRegion] = React.useState<string>('');
  React.useEffect(() => {
    setRegion(
      (regionData.primary.length > 0 && regionData.primary[0].region + '#' + regionData.primary[0].zone + '#PRIMARY') || 
      (regionData.readReplica.length > 0 && regionData.readReplica[0].region + '#' + regionData.readReplica[0].zone + '#READ_REPLICA') ||
      ''
    )
  }, [regionData]);
  const [selectedRegion, selectedZone, clusterType] = region ? region.split('#') : [undefined, undefined, undefined];

  const isMultiRegionEnabled = true;

  const { data: clusterData } = useGetClusterQuery();
  const { data: healthCheckData } = useGetClusterHealthCheckQuery();
  var cluster = clusterData?.data;
  var health = healthCheckData?.data;
  return (
    <>
      <div className={classes.widgets}>
        {cluster && isMultiRegionEnabled && <ClusterInfoWidget cluster={cluster} />}
        {cluster && !isMultiRegionEnabled && <ClusterInfo cluster={cluster} />}
        {cluster && health && isMultiRegionEnabled &&
          <ClusterStatusWidget cluster={cluster} health={health} />}
      </div>
      <div className={classes.metricsRow}>
        <Box display="flex" gridGap={10} alignItems="center" width="100%">
          <Box mr={1}>
            <Typography variant="h5">{t('clusterDetail.keyMetrics')}</Typography>
          </Box>
          <YBSelect
            className={classes.intervalPicker}
            value={region}
            onChange={(event) => setRegion(event.target.value)}
          >
            {regionData.primary.length > 0 &&
              <Typography variant="body2" className={classes.dropdownTitle}>{t('clusterDetail.primaryCluster')}</Typography>
            }
            {regionData.primary.map(data => (
              <MenuItem key={data.region + '#' + data.zone + '#PRIMARY'} value={data.region + '#' + data.zone + '#PRIMARY'}>
                {data.flag && <Box mr={1}>{data.flag}</Box>} {data.region} ({data.zone})
              </MenuItem>
            ))}
            {regionData.primary.length > 0 && regionData.readReplica.length > 0 &&
              <Divider className={classes.divider} />
            }
            {regionData.readReplica.length > 0 &&
                <Typography variant="body2" className={classes.dropdownTitle}>{t('clusterDetail.readReplicas')}</Typography>
            }
            {regionData.readReplica.map(data => (
              <MenuItem key={data.region + '#' + data.zone + '#READ_REPLICA'} value={data.region + '#' + data.zone + '#READ_REPLICA'}>
                {data.flag && <Box mr={1}>{data.flag}</Box>} {data.region} ({data.zone})
              </MenuItem>
            ))}
          </YBSelect>
          <YBSelect
            className={classes.intervalPicker}
            value={interval}
            onChange={(event) => changeInterval(event.target.value)}
          >
            <MenuItem value={RelativeInterval.LastHour}>
                {t('clusterDetail.lastHour')}
            </MenuItem>
            <MenuItem value={RelativeInterval.Last6Hours}>
                {t('clusterDetail.last6hours')}
            </MenuItem>
            <MenuItem value={RelativeInterval.Last12hours}>
                {t('clusterDetail.last12hours')}
            </MenuItem>
            <MenuItem value={RelativeInterval.Last24hours}>
                {t('clusterDetail.last24hours')}
            </MenuItem>
            {/*<MenuItem value={RelativeInterval.Last7days}>
                {t('clusterDetail.last7days')}
            </MenuItem>*/}
          </YBSelect>          
        </Box>
      </div>
      <Grid container spacing={2}>
        {chartList.map((chart: string) => {
          const config = chartConfig[chart];
          return (
            config && (
              <Grid key={chart} item xs={6}>
                <ChartController
                  title={config.title}
                  metric={config.metric}
                  unitKey={config.unitKey}
                  metricChartLabels={config.chartLabels}
                  strokes={config.strokes}
                  chartDrawingType={config.chartDrawingType}
                  relativeInterval={interval}
                  regionName={selectedRegion}
                  zone={selectedZone}
                  clusterType={clusterType as (ClusterType | undefined)}
                />
              </Grid>
            )
          );
        })}
      </Grid>
    </>
  );
};
