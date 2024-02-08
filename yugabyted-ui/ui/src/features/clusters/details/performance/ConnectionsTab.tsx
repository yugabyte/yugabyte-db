import React, { FC, useState, useMemo } from "react";
import {
    Box,
    Grid,
    makeStyles,
    Paper,
    Typography,
    MenuItem,
    LinearProgress
} from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBButton, YBSelect } from "@app/components";
import RefreshIcon from "@app/assets/refresh.svg";
import { useChartConfig } from '@app/features/clusters/details/overview/ChartConfig';
import { ChartController } from '@app/features/clusters/details/overview/ChartControler';
import { RelativeInterval } from '@app/helpers';
import { useGetClusterNodesQuery } from '@app/api/src';


const useStyles = makeStyles((theme) => ({
  divider: {
    marginBottom: theme.spacing(2)
  },
  selectBox: {
    minWidth: "200px",
  },
  paperContainer: {
    padding: theme.spacing(3),
    paddingBottom: theme.spacing(4),
    marginBottom: theme.spacing(2),
    border: `1px solid ${theme.palette.grey[200]}`,
    width: "100%",
  },
  heading: {
    marginBottom: theme.spacing(3),
  },
}));

const chartName = 'totalConnections';

export const ConnectionsTab: FC = () => {
  const classes = useStyles();
  const { t } = useTranslation();

  const chartConfig = useChartConfig();
  const config = chartConfig[chartName];

  const ALL_NODES = { label: t('clusterDetail.performance.metrics.allNodes'), value: 'all' };

  const [nodeName, setNodeName] = useState<string | undefined>(ALL_NODES.value);
  const [relativeInterval, setRelativeInterval] = useState<string>(RelativeInterval.Last5Minutes);
  const { data: nodesResponse, refetch: refetchNodes, isLoading: isClusterNodesLoading } =
      useGetClusterNodesQuery();
  const nodesNamesList = useMemo(() => [
    ALL_NODES,
    ...(nodesResponse?.data.map((node) => ({ label: node.name, value: node.name })) ?? [])
  ], [nodesResponse?.data]);

  const [refreshChart, doRefreshChart] = useState(0);

  const refresh = () => {
    refetchNodes();
    doRefreshChart((prev) => prev + 1);
  }

  if (isClusterNodesLoading) {// || runtimeConfigLoading || runtimeConfigAccountLoading) {
    return <LinearProgress />;
  }

  return (
    <Box>
      <Paper className={classes.paperContainer}>
        <Box display="flex" justifyContent="space-between" alignItems="start">
          <Typography variant="h4" className={classes.heading}>
            {t("clusterDetail.performance.tabs.connections")}
          </Typography>
          <YBButton variant="ghost" startIcon={<RefreshIcon />} onClick={refresh}>
            {t("clusterDetail.performance.actions.refresh")}
          </YBButton>
        </Box>
        <Grid container justifyContent="space-between" alignItems="center">
          <Grid item xs={1}>
            <Box display="flex">
              <YBSelect
                className={classes.selectBox}
                value={nodeName}
                onChange={(e) => {
                  setNodeName((e.target as HTMLInputElement).value);
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
                  setRelativeInterval((e.target as HTMLInputElement).value);
                }}
              >
                  <MenuItem value={RelativeInterval.Last5Minutes}>
                    {t('clusterDetail.last5minutes')}
                  </MenuItem>
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
              </YBSelect>
            </Box>
          </Grid>
        </Grid>
        <div className={classes.divider} />
        <Grid container spacing={2}>
          <Grid item xs={12}>
            <ChartController
              nodeName={nodeName}
              title={config.title}
              metric={config.metric}
              unitKey={config.unitKey}
              metricChartLabels={config.chartLabels}
              strokes={config.strokes}
              chartDrawingType={config.chartDrawingType}
              relativeInterval={relativeInterval as RelativeInterval}
              refreshFromParent={refreshChart}
            />
          </Grid>
        </Grid>
      </Paper>
    </Box>
  );
};
