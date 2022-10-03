import React, { FC, useState } from 'react';
import { Box, Grid, makeStyles, MenuItem, Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { useHistory, useLocation } from 'react-router-dom';

// Local imports
import { YBSelect } from '@app/components';
// import { YBSelect, RegionSelector } from '@app/components';
import { ClusterInfo } from './ClusterInfo';
import { ClusterInfoWidget } from './ClusterInfoWidget';
import { RelativeInterval } from '@app/helpers';
import { useChartConfig } from '@app/features/clusters/details/overview/ChartConfig';
import { ChartController } from '@app/features/clusters/details/overview/ChartControler';
import { useGetClusterQuery } from '@app/api/src';


const useStyles = makeStyles((theme) => ({
  metricsRow: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'space-between',
    margin: theme.spacing(2, 0)
  },
  intervalPicker: {
    minWidth: 200
  }
}));

const SEARCH_PARAM_INTERVAL = 'interval';

const chartList = ['operations', 'latency', 'cpuUsage', 'diskUsage'];
export const MULTI_REGION_FLAG_PATH = 'ybcloud.conf.cluster.geo_partitioning.enabled';

export const OverviewTab: FC = () => {
  const classes = useStyles();
  const { t } = useTranslation();
  const history = useHistory();
  const location = useLocation();
  const chartConfig = useChartConfig();
  const [selectedRegion] = useState<string>();
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

  const isMultiRegionEnabled = true;

  const { data: clusterData } = useGetClusterQuery()
  var cluster = clusterData?.data
  return (
    <>
      {cluster && isMultiRegionEnabled && <ClusterInfoWidget cluster={cluster} />}
      {cluster && !isMultiRegionEnabled && <ClusterInfo cluster={cluster} />}
      <div className={classes.metricsRow}>
        <Box display="flex" justifyContent="space-between" alignItems="center" width="100%">          
          <Box>
            <Typography variant="h5">{t('clusterDetail.keyMetrics')}</Typography>
          </Box>
          <YBSelect
            className={classes.intervalPicker}
            value={interval}
            onChange={(event) => changeInterval(event.target.value)}
          >
            <MenuItem value={RelativeInterval.LastHour}>{t('clusterDetail.lastHour')}</MenuItem>
            <MenuItem value={RelativeInterval.Last6Hours}>{t('clusterDetail.last6hours')}</MenuItem>
            <MenuItem value={RelativeInterval.Last12hours}>{t('clusterDetail.last12hours')}</MenuItem>
            <MenuItem value={RelativeInterval.Last24hours}>{t('clusterDetail.last24hours')}</MenuItem>
            {/*<MenuItem value={RelativeInterval.Last7days}>{t('clusterDetail.last7days')}</MenuItem>*/}
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
                />
              </Grid>
            )
          );
        })}
      </Grid>
    </>
  );
};
