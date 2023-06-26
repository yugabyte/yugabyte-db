import React, { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Box, Grid, Link, makeStyles, Typography } from '@material-ui/core';
import { ChevronRight } from '@material-ui/icons';
import clsx from 'clsx';
import { formatDistance } from 'date-fns';
import { BadgeVariant, YBBadge } from '@app/components/YBBadge/YBBadge';
import { Link as RouterLink } from 'react-router-dom';

const useStyles = makeStyles((theme) => ({
  divider: {
    width: '100%',
    marginLeft: 0,
    marginTop: theme.spacing(2.5),
    marginBottom: theme.spacing(2.5),
  },
  container: {
    flexWrap: "nowrap",
    marginTop: theme.spacing(1.5),
    justifyContent: 'center',
    width: theme.spacing(40),
  },
  title: {
    color: theme.palette.grey[900],
    fontWeight: theme.typography.fontWeightRegular as number,
  },
  label: {
    color: theme.palette.grey[600],
    marginTop: theme.spacing(0.625),
  },
  margin: {
    marginTop: theme.spacing(2.3),
    marginBottom: theme.spacing(2.5),
  },
  arrow: {
    color: theme.palette.grey[600],
    marginTop: theme.spacing(0.5)
  },
  alertContainer: {
    width: "100%",
  },
  alertContent: {
    display: "flex",
    gap: theme.spacing(1),
    justifyContent: "space-between",
    alignItems: "center",
    marginTop: theme.spacing(0.23),
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

interface ClusterAlertWidgetProps {
}

// const date = new Date();

// Sample alert for now
const alerts: any[] = [
  /* {
    alert: "CPU usage exceeds 75% for node 123",
    at: date.setMinutes(date.getMinutes() - 25),
    status: "Warning"
  } */
]

export const ClusterAlertWidget: FC<ClusterAlertWidgetProps> = () => {
  const classes = useStyles();
  const { t } = useTranslation();

  return (
    <Box>
      <Link className={classes.link} component={RouterLink} to="/alerts/tabNotifications">
        <Box display="flex" alignItems="center">
          <Box display="flex" alignItems="center" flex={1} gridGap={8}>
            <Typography variant="body2" className={classes.title}>{t('clusterDetail.overview.alerts')}</Typography>
            {alerts.length > 0 && 
              <YBBadge variant={BadgeVariant.Warning} text={alerts.length} icon={false} />
            }
          </Box>
          <ChevronRight className={classes.arrow} />
        </Box>
        <Grid container className={classes.container}>
          {alerts.length === 0 ?
            <Typography variant="body2" className={clsx(classes.label, classes.margin)}>
              {t('clusterDetail.overview.noAlerts')}
            </Typography>
            :
            <Box className={classes.alertContainer}>
              <Typography variant="body2" className={classes.label}>
                {formatDistance(alerts[0].at, new Date(), { addSuffix: true })}
              </Typography>
              <Box className={classes.alertContent}>
                <Typography variant="body2" className={classes.title} noWrap>
                  {alerts[0].alert}
                </Typography>
                <Box className={classes.statusContainer}>
                  <YBBadge variant={BadgeVariant.Warning} text={alerts[0].status} />
                </Box>
              </Box>
            </Box>
          }
        </Grid>
      </Link>
    </Box>
  );
};
