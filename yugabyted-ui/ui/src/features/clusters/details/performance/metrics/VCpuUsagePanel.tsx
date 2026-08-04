import React, { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Box, makeStyles, Paper, Typography } from '@material-ui/core';
import { VCpuUsageSankey } from '../../overview/VCpuUsageSankey';
import { ClusterData, useGetClusterNodesQuery } from '@app/api/src';
import { YBButton } from '@app/components';
import RefreshIcon from '@app/assets/refresh.svg';

const useStyles = makeStyles((theme) => ({
  container: {
    padding: theme.spacing(1.5, 2, 2, 2),
    marginBottom: theme.spacing(2.5),
  },
  chartContainer: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
    padding: theme.spacing(1, 1, 0, 1),
  },
}));

interface VCpuUsagePanelProps {
  cluster: ClusterData,
}

export const VCpuUsagePanel: FC<VCpuUsagePanelProps> = ({ cluster }) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const { refetch: refetchNodes } = useGetClusterNodesQuery({}, { query: { enabled: false }});

  return (
    <Paper className={classes.container}>
      <Box display="flex" alignItems="center" justifyContent="space-between">
        <Typography variant="h5">{t('clusterDetail.performance.metrics.vCpuUsage')}</Typography>
        <YBButton variant="ghost" startIcon={<RefreshIcon />} onClick={() => refetchNodes()}>
          {t('clusterDetail.performance.actions.refresh')}
        </YBButton>
      </Box>
      <Box className={classes.chartContainer}>
        <VCpuUsageSankey cluster={cluster}
          width={650}
          height={200}
          sankeyProps={{
            margin: {
              top: 6,
              left: 168,
              right: 225,
              bottom: 5,
            },
          }} />
      </Box>
    </Paper>
  );
};
