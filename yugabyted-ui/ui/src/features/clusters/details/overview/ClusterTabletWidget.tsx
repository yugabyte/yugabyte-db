import React, { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Box, Grid, makeStyles, Typography } from '@material-ui/core';
import { useGetClusterTabletsQuery } from '@app/api/src';
import { ChevronRight } from '@material-ui/icons';

// Local imports
import type { HealthCheckInfo } from '@app/api/src';
import clsx from 'clsx';
import { STATUS_TYPES, YBStatus } from '@app/components';

const useStyles = makeStyles((theme) => ({
  container: {
    flexWrap: "nowrap",
    marginTop: theme.spacing(1.5),
    justifyContent: 'space-between',
  },
  section: {
    marginRight: theme.spacing(2),
  },
  sectionInvisible: {
    marginRight: theme.spacing(4),
  },
  sectionBorder: {
    paddingLeft: theme.spacing(2),
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
    textAlign: 'left',
    paddingTop: theme.spacing(0.25)
  },
  arrow: {
    color: theme.palette.grey[600],
    marginTop: theme.spacing(0.5)
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

  return (
    <Box>
      <Box display="flex" alignItems="center">
        <Typography variant="body2" className={classes.title}>{t('clusterDetail.overview.tablets')}</Typography>
        <ChevronRight className={classes.arrow} />
      </Box>
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
          <Box display="flex" gridGap={7}>
            <Typography variant="h4" className={classes.value}>
              {underReplicatedTablets.length}
            </Typography>
            <YBStatus value={underReplicatedTablets.length} type={STATUS_TYPES.WARNING} tooltip />
          </Box>
          <Typography variant="body2" className={classes.label}>
            {t('clusterDetail.overview.underReplicated')}
          </Typography>
        </div>
        <div className={classes.section}>
          <Box display="flex" gridGap={7}>
            <Typography variant="h4" className={classes.value}>
              {unavailableTablets.length}
            </Typography>
            <YBStatus value={unavailableTablets.length} type={STATUS_TYPES.FAILED} tooltip />
          </Box>
          <Typography variant="body2" className={classes.label}>
            {t('clusterDetail.overview.unavailable')}
          </Typography>
        </div>
        <div className={classes.sectionInvisible}></div>
      </Grid>
    </Box>
  );
};
