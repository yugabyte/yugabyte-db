import React, { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Grid, makeStyles, Paper, Typography } from '@material-ui/core';
import { useGetClusterTabletsQuery } from '@app/api/src';

// Local imports
import type { HealthCheckInfo } from '@app/api/src';

const useStyles = makeStyles((theme) => ({
  clusterInfo: {
    padding: theme.spacing(2),
    flexGrow: 1,
    flexBasis: 0,
    border: `1px solid ${theme.palette.grey[200]}`
  },
  container: {
    justifyContent: 'space-between'
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
  },
}));

interface ClusterTabletWidgetProps {
  health: HealthCheckInfo;
}

export const ClusterTabletWidget: FC<ClusterTabletWidgetProps> = ({ health }) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const { data: tablets } = useGetClusterTabletsQuery();

  const tabletsList = tablets?.data ?? {};
  const tabletIds = Object.keys(tabletsList);
  const totalTablets = tabletIds.length;
  const underReplicatedTablets = health?.under_replicated_tablets ?? [];
  const unavailableTablets = health?.leaderless_tablets ?? [];
//   const unavailableTablets = tabletIds.filter((key) => {
//     !tabletsList[key].has_leader
//   });
  const numUnavailableTablets = unavailableTablets.length


  return (
    <Paper className={classes.clusterInfo}>
      <Grid container className={classes.container}>
        <div>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusterDetail.overview.totalTablets')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {totalTablets}
          </Typography>
        </div>
        <div>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusterDetail.overview.underReplicatedTablets')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {underReplicatedTablets.length}
          </Typography>
        </div>
        <div>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusterDetail.overview.unavailableTablets')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {numUnavailableTablets}
          </Typography>
        </div>
      </Grid>
    </Paper>
  );
};
