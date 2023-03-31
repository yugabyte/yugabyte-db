import React, { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Box, makeStyles, Paper, Typography } from '@material-ui/core';
import { VCpuUsageSankey } from '../../overview/VCpuUsageSankey';
import type { ClusterData } from '@app/api/src';

const useStyles = makeStyles((theme) => ({
  container: {
    padding: theme.spacing(2),
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

  return (
    <Paper className={classes.container}>
      <Typography variant="h5">{t('clusterDetail.performance.metrics.vCpuUsage')}</Typography>
      <Box className={classes.chartContainer}>
        <VCpuUsageSankey cluster={cluster}
          width={650}
          height={200}
          sankeyProps={{
            margin: {
              top: 6,
              left: 168,
              right: 180,
              bottom: 0,
            },
          }} />
      </Box>
    </Paper>
  );
};