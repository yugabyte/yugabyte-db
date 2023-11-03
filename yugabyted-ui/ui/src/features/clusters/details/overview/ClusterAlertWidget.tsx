import React, { FC, useMemo } from "react";
import { useTranslation } from "react-i18next";
import { Box, Grid, Link, makeStyles, Typography } from "@material-ui/core";
import { ChevronRight } from "@material-ui/icons";
import clsx from "clsx";
import { BadgeVariant, YBBadge } from "@app/components/YBBadge/YBBadge";
import { Link as RouterLink } from "react-router-dom";
import { useAlerts } from "../alerts/alerts";

const useStyles = makeStyles((theme) => ({
  divider: {
    width: "100%",
    marginLeft: 0,
    marginTop: theme.spacing(2.5),
    marginBottom: theme.spacing(2.5),
  },
  container: {
    flexWrap: "nowrap",
    marginTop: theme.spacing(1.5),
    justifyContent: 'center',
    minWidth: theme.spacing(40),
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
    marginTop: theme.spacing(0.5),
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
    "&:link, &:focus, &:active, &:visited, &:hover": {
      textDecoration: "none",
      color: theme.palette.text.primary,
    },
  },
}));

interface ClusterAlertWidgetProps {}

export const ClusterAlertWidget: FC<ClusterAlertWidgetProps> = () => {
  const classes = useStyles();
  const { t } = useTranslation();

  const { data: alertNotifications } = useAlerts(true);

  const alert = useMemo<(typeof alertNotifications)[number] | undefined>(() => {
    if (alertNotifications.length === 0) {
      return undefined;
    }

    const sortedAlerts = alertNotifications.slice().sort((a, b) => {
      const order = [BadgeVariant.Error, BadgeVariant.Warning, BadgeVariant.Info];
      return order.indexOf(a.status) - order.indexOf(b.status);
    });

    return sortedAlerts[0];
  }, [alertNotifications]);

  return (
    <Box>
      <Link className={classes.link} component={RouterLink} to="/alerts/tabNotifications">
        <Box display="flex" alignItems="center">
          <Box display="flex" alignItems="center" flex={1} gridGap={8}>
            <Typography variant="body2" className={classes.title}>
              {t("clusterDetail.overview.alerts")}
            </Typography>
            {alertNotifications.length > 0 && (
              <YBBadge variant={alert?.status} text={alertNotifications.length} icon={false} />
            )}
          </Box>
          <ChevronRight className={classes.arrow} />
        </Box>
        <Grid container className={classes.container}>
          {alertNotifications.length === 0 ? (
            <Typography variant="body2" className={clsx(classes.label, classes.margin)}>
              {t("clusterDetail.overview.noAlerts")}
            </Typography>
          ) : (
            <Box className={classes.alertContainer}>
              <Typography variant="body2" className={classes.label}>
                {alert?.key}
              </Typography>
              <Box className={classes.alertContent}>
                <Typography variant="body2" className={classes.title} noWrap>
                  {alert?.title}
                </Typography>
                <Box className={classes.statusContainer}>
                  <YBBadge
                    variant={alert?.status}
                    text={
                      alert?.status === BadgeVariant.Error
                        ? t("clusterDetail.alerts.configuration.severe")
                        : alert?.status === BadgeVariant.Warning
                        ? t("clusterDetail.alerts.configuration.warning")
                        : undefined
                    }
                  />
                </Box>
              </Box>
            </Box>
          )}
        </Grid>
      </Link>
    </Box>
  );
};
