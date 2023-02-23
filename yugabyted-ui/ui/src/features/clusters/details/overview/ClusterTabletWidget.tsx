import React, { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Box, Grid, makeStyles, Typography } from '@material-ui/core';
import { useGetClusterTabletsQuery } from '@app/api/src';

// Local imports
import type { HealthCheckInfo } from '@app/api/src';
import clsx from 'clsx';

const useStyles = makeStyles((theme) => ({
  container: {
    flexWrap: "nowrap",
    marginTop: theme.spacing(1.5),
  },
  section: {
    marginRight: theme.spacing(2),
  },
  sectionBorder: {
    paddingLeft: theme.spacing(2),
    marginLeft: theme.spacing(11.75),
    borderLeft: `1px solid ${theme.palette.grey[300]}`
  },
  title: {
    color: theme.palette.grey[900],
    fontWeight: theme.typography.fontWeightRegular as number,
    flexGrow: 1,
  },
  label: {
    color: theme.palette.grey[500],
    fontWeight: theme.typography.fontWeightMedium as number,
    marginTop: theme.spacing(1.25),
    marginBottom: theme.spacing(0.75),
    textTransform: 'uppercase',
    textAlign: 'left'
  },
  value: {
    color: theme.palette.grey[900],
    textAlign: 'left'
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
//   const unavailableTablets = health?.leaderless_tablets ?? [];
//   const unavailableTablets = tabletIds.filter((key) => {
//     !tabletsList[key].has_leader
//   });
//   const numUnavailableTablets = unavailableTablets.length


  return (
    <Box>
      <Typography variant="body2" className={classes.title}>{t('clusterDetail.overview.tablets')}</Typography>
      <Grid container className={classes.container}>
        <div className={classes.section}>
          <Typography variant="h4" className={classes.value}>
            {totalTablets}
          </Typography>
          <Typography variant="body2" className={classes.label}>
            {t('clusterDetail.overview.total')}
          </Typography>
        </div>
        <div className={clsx(classes.section, classes.sectionBorder)}>
          <Typography variant="h4" className={classes.value}>
            {underReplicatedTablets.length}
          </Typography>
          <Typography variant="body2" className={classes.label}>
            {t('clusterDetail.overview.underReplicated')}
          </Typography>
        </div>
      </Grid>
    </Box>
  );
};
