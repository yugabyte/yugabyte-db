import React, { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Box, Divider, Grid, /* Link, */ makeStyles, Typography } from '@material-ui/core';
import { ChevronRight } from '@material-ui/icons';
import clsx from 'clsx';
import { formatDistance } from 'date-fns';
import { BadgeVariant, YBBadge } from '@app/components/YBBadge/YBBadge';
import { ClusterAlertWidget } from './ClusterAlertWidget';
/* import { Link as RouterLink } from 'react-router-dom'; */

const useStyles = makeStyles((theme) => ({
  divider: {
    width: '100%',
    marginLeft: 0,
    marginTop: theme.spacing(2.5),
    marginBottom: theme.spacing(1.5),
  },
  container: {
    flexWrap: "nowrap",
    marginTop: theme.spacing(1.5),
    justifyContent: "center",
    minWidth: theme.spacing(40),
  },
  title: {
    /* color: theme.palette.grey[900], */
    color: theme.palette.grey[500],
    fontWeight: theme.typography.fontWeightRegular as number,
    flexGrow: 1,
  },
  label: {
    /* color: theme.palette.grey[600], */
    color: theme.palette.grey[500],
    marginTop: theme.spacing(0.625),
  },
  margin: {
    marginTop: theme.spacing(2.5),
    marginBottom: theme.spacing(2.6),
  },
  arrow: {
    /* color: theme.palette.grey[600], */
    color: theme.palette.grey[400],
    marginTop: theme.spacing(0.5)
  },
  activityContainer: {
    width: "100%",
  },
  activityContent: {
    display: "flex",
    gap: theme.spacing(1),
    justifyContent: "space-between",
    alignItems: "center",
    marginTop: theme.spacing(0.23),
    marginBottom: theme.spacing(0.925),
  },
  statusContainer: {
    display: "flex",
    alignItems: "center",
    gap: theme.spacing(1),
  },
  link: {
    '&:link, &:focus, &:active, &:visited, &:hover': {
      textDecoration: 'none',
      color: theme.palette.text.primary
    }
  }
}));

interface ClusterActivityWidgetProps {
}

// const date = new Date();

// Sample activity for now
const activities: any[] = [
  /* {
    activity: "Add node",
    at: date.setMinutes(date.getMinutes() - 3),
    status: "In progress"
  } */
]

export const ClusterActivityWidget: FC<ClusterActivityWidgetProps> = () => {
  const classes = useStyles();
  const { t } = useTranslation();

  return (
    <Box>
      {/* <Link className={classes.link} component={RouterLink} to="tabActivity"> */}
        <Box display="flex" alignItems="center">
          <Typography variant="body2" className={classes.title}>{t('clusterDetail.overview.activities')}</Typography>
          <ChevronRight className={classes.arrow} />
        </Box>
        <Grid container className={classes.container}>
          {activities.length === 0 ?
            <Typography variant="body2" className={clsx(classes.label, classes.margin)}>
              {/* {t('clusterDetail.overview.noActivities')} */}
              {t('clusterDetail.overview.comingsoon')}
            </Typography>
            :
            <Box className={classes.activityContainer}>
              <Typography variant="body2" className={classes.label}>
                {formatDistance(activities[0].at, new Date(), { addSuffix: true })}
              </Typography>
              <Box className={classes.activityContent}>
                <Typography variant="body2" className={classes.title} noWrap>
                  {activities[0].activity}
                </Typography>
                <Box className={classes.statusContainer}>
                  <YBBadge variant={BadgeVariant.InProgress} text={activities[0].status} />
                </Box>
              </Box>
            </Box>
          }
        </Grid>
      {/* </Link> */}
      <Divider orientation="horizontal" variant="middle" className={classes.divider} />
      <ClusterAlertWidget />
    </Box>
  );
};
