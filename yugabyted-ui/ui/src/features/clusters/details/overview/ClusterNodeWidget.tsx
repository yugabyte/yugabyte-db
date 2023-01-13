import React, { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Grid, makeStyles, Paper, Typography } from '@material-ui/core';

// Local imports
import type { ClusterData, HealthCheckInfo } from '@app/api/src';

const useStyles = makeStyles((theme) => ({
  clusterInfo: {
    padding: theme.spacing(2),
    flexGrow: 1,
    flexBasis: 0,
    border: `1px solid ${theme.palette.grey[200]}`
  },
  container: {
    justifyContent: 'space-around'
  },
  label: {
    color: theme.palette.grey[600],
    fontWeight: theme.typography.fontWeightMedium as number,
    marginBottom: theme.spacing(0.75),
    textTransform: 'uppercase',
    textAlign: 'center'
  },
  value: {
    paddingTop: theme.spacing(0.57),
    textAlign: 'center'
  }
}));

interface ClusterNodeWidgetProps {
  cluster: ClusterData;
  health: HealthCheckInfo;
}

export const ClusterNodeWidget: FC<ClusterNodeWidgetProps> = ({ cluster, health }) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const clusterSpec = cluster?.spec;
  const numNodes = clusterSpec?.cluster_info?.num_nodes ?? 0;
  const deadNodes = health?.dead_nodes;
  // TODO: get boostrappingNodes value from api
  // const bootstrappingNodes = 0;

  return (
    <Paper className={classes.clusterInfo}>
      <Grid container className={classes.container}>
        <div>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusterDetail.overview.totalNodes')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {numNodes}
          </Typography>
        </div>
        <div>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusterDetail.overview.deadNodes')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {deadNodes.length}
          </Typography>
        </div>
        {/*<div>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusterDetail.overview.bootstrappingNodes')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {bootstrappingNodes}
          </Typography>
        </div>*/}
      </Grid>
    </Paper>
  );
};
