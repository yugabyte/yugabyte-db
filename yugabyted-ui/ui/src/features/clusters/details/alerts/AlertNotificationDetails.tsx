import React, { FC, useMemo } from "react";
import { Box, makeStyles, useTheme } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBTable, YBLoadingBox, YBCheckbox } from "@app/components";
import { BadgeVariant, YBBadge } from "@app/components/YBBadge/YBBadge";
import type { TFunction } from "i18next";
import clsx from "clsx";
import { useLocalStorage } from "react-use";
import { AlertConfiguration, alertConfigurationsKey, alertList, useCPUAlert } from "./alerts";

const useStyles = makeStyles((theme) => ({
  sectionWrapper: {
    display: "flex",
    gap: theme.spacing(2),
  },
  filtersSection: {
    flex: 1,
    paddingTop: theme.spacing(2),
    borderRight: `1px solid ${theme.palette.grey[200]}`,
  },
  filterHeading: {
    fontWeight: 500,
    fontSize: "11.5px",
    color: theme.palette.grey[600],
    textTransform: "uppercase",
    marginTop: theme.spacing(2),
    marginBottom: theme.spacing(0.5),
  },
  notificationsSection: {
    flex: 5,
    paddingTop: theme.spacing(2),
  },
  sectionHeading: {
    fontWeight: 700,
    fontSize: "15px",
    color: theme.palette.grey[900],
    marginBottom: theme.spacing(1),
  },
  notificationWrapper: {
    display: "flex",
    alignItems: "center",
    gap: theme.spacing(1),
  },
  notificationContent: {
    display: "flex",
    flexDirection: "column",
    margin: theme.spacing(0.5, 0),
    gap: theme.spacing(0.5),
  },
  notificationTitle: {
    color: theme.palette.grey[800],
    fontWeight: 600,
    fontSize: "13px",
    "&:first-letter": {
      textTransform: "uppercase",
    },
  },
  notificationDescription: {
    color: theme.palette.grey[600],
    fontWeight: 400,
    fontSize: "12px",
  },
  sevIndicator: {
    padding: theme.spacing(3, 0),
    width: theme.spacing(0.65),
    minWidth: theme.spacing(0.65),
    borderRadius: theme.shape.borderRadius,
  },
  sevIndicatorSm: {
    padding: theme.spacing(1.2, 0),
  },
  sevIndicatorWrapper: {
    display: "flex",
    gap: theme.spacing(1),
  },
  checkbox: {
    padding: theme.spacing(0.5, 1, 0.5, 0),
  },
}));

type Notification = {
  key: string;
  title?: string;
  info: string;
  status: BadgeVariant;
};

const SevIndicator: React.FC<{ status: BadgeVariant; smIndicator?: boolean }> = ({
  status,
  smIndicator,
  children,
}) => {
  const theme = useTheme();
  const classes = useStyles();

  const backgroundColor =
    status === BadgeVariant.Error
      ? theme.palette.error[500]
      : status === BadgeVariant.Warning
      ? theme.palette.warning[500]
      : theme.palette.info[500];

  return (
    <Box className={classes.sevIndicatorWrapper}>
      <Box
        className={clsx(classes.sevIndicator, smIndicator && classes.sevIndicatorSm)}
        style={{ backgroundColor }}
      />
      {children}
    </Box>
  );
};

const NotificationComponent =
  (classes: ReturnType<typeof useStyles>, notifications: Notification[]) => (dataIndex: number) => {
    const notification = notifications[dataIndex];

    return (
      <Box className={classes.notificationWrapper}>
        <SevIndicator status={notification.status}>
          <Box className={classes.notificationContent}>
            <Box className={classes.notificationTitle}>{notification.title}</Box>
            <Box className={classes.notificationDescription}>{notification.info}</Box>
          </Box>
        </SevIndicator>
      </Box>
    );
  };

const StatusComponent = (t: TFunction) => (status: BadgeVariant) => {
  const badgeText =
    status === BadgeVariant.Error
      ? t("clusterDetail.alerts.configuration.severe")
      : status === BadgeVariant.Warning
      ? t("clusterDetail.alerts.configuration.warning")
      : undefined;

  return (
    <Box>
      <YBBadge variant={status} text={badgeText} icon={false} />
    </Box>
  );
};

export const AlertNotificationDetails: FC = () => {
  const { t } = useTranslation();
  const classes = useStyles();

  const [severeFilter, setSevereFilter] = React.useState<boolean>(false);
  const [warningFilter, setWarningFilter] = React.useState<boolean>(false);

  const cpuAlerts = useCPUAlert();

  const [config] = useLocalStorage<AlertConfiguration[]>(alertConfigurationsKey);

  const upstreamNotificationData = useMemo(
    () => [
      {
        name: "ntp/chrony",
        info: "ntp/chrony package is missing for clock synchronization. For centos 7, we recommend installing either ntp or chrony package and for centos 8, we recommend installing chrony package.",
      },
      {
        name: "insecure",
        info: "Cluster started in an insecure mode without authentication and encryption enabled. For non-production use only, not to be used without firewalls blocking the internet traffic.",
      },
    ],
    []
  );

  const notificationData = useMemo<Notification[]>(() => {
    const cpuNotifications = [];
    if (cpuAlerts.cpu_90) {
      const alert = alertList.find((alert) => alert.key === Object.keys(cpuAlerts)[0])!;
      cpuNotifications.push({
        key: alert.key,
        title: "CPU Utilization",
        info: alert.name,
        status: BadgeVariant.Error,
      });
    } else if (cpuAlerts.cpu_75) {
      const alert = alertList.find((alert) => alert.key === Object.keys(cpuAlerts)[1])!;
      cpuNotifications.push({
        key: alert.key,
        title: "CPU Utilization",
        info: alert.name,
        status: BadgeVariant.Warning,
      });
    }

    const apiNotifications = upstreamNotificationData.map((notificationData) => ({
      key: notificationData.name,
      title:
        alertList.find((alert) => alert.key === notificationData.name)?.name ||
        BadgeVariant.Warning,
      info: notificationData.info,
      status: BadgeVariant.Warning,
    }));

    return [...cpuNotifications, ...apiNotifications];
  }, [upstreamNotificationData, cpuAlerts]);

  const filteredNotificationData = useMemo(() => {
    let filteredNotifications: typeof notificationData;

    /* Sev filter */
    filteredNotifications = notificationData.filter(
      (notification) =>
        (severeFilter && notification.status === BadgeVariant.Error) ||
        (warningFilter && notification.status === BadgeVariant.Warning) ||
        (!severeFilter && !warningFilter)
    );

    /* Conf filter */
    filteredNotifications = filteredNotifications.filter(
      (notification) => config?.find((config) => config.key === notification.key)?.enabled ?? true
    );

    return filteredNotifications;
  }, [notificationData, severeFilter, warningFilter, config]);

  const notificationColumns = [
    {
      name: "title",
      label: t("clusterDetail.alerts.notification.name"),
      options: {
        customBodyRenderLite: NotificationComponent(classes, filteredNotificationData),
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "status",
      label: t("clusterDetail.alerts.notification.severity"),
      options: {
        customBodyRender: StatusComponent(t),
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px", minWidth: 150 } }),
      },
    },
  ];

  return (
    <Box className={classes.sectionWrapper}>
      <Box className={classes.filtersSection}>
        <Box className={classes.sectionHeading}>
          {t("clusterDetail.alerts.notification.filters")}
        </Box>
        <Box className={classes.filterHeading}>
          {t("clusterDetail.alerts.notification.severity")}
        </Box>
        <Box>
          <YBCheckbox
            value={severeFilter}
            onChange={(event: React.ChangeEvent<HTMLInputElement>) =>
              setSevereFilter(event.target.checked)
            }
            className={classes.checkbox}
            label={
              <SevIndicator status={BadgeVariant.Error} smIndicator>
                {t("clusterDetail.alerts.notification.severe")}
              </SevIndicator>
            }
          />
        </Box>
        <Box>
          <YBCheckbox
            value={warningFilter}
            onChange={(event: React.ChangeEvent<HTMLInputElement>) =>
              setWarningFilter(event.target.checked)
            }
            className={classes.checkbox}
            label={
              <SevIndicator status={BadgeVariant.Warning} smIndicator>
                {t("clusterDetail.alerts.notification.warning")}
              </SevIndicator>
            }
          />
        </Box>
      </Box>
      <Box className={classes.notificationsSection}>
        <Box className={classes.sectionHeading}>
          {t("clusterDetail.alerts.notification.xnotifications", {
            count: filteredNotificationData.length,
          })}
        </Box>
        {filteredNotificationData.length ? (
          <Box pb={4} pt={1}>
            <YBTable
              data={filteredNotificationData}
              columns={notificationColumns}
              touchBorder={false}
            />
          </Box>
        ) : (
          <YBLoadingBox>{t("clusterDetail.alerts.notification.nonotifications")}</YBLoadingBox>
        )}
      </Box>
    </Box>
  );
};
