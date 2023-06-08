import React, { FC, useMemo } from "react";
import { Box, makeStyles, useTheme } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBTable, YBLoadingBox, YBCheckbox } from "@app/components";
import { BadgeVariant, YBBadge } from "@app/components/YBBadge/YBBadge";
import type { TFunction } from "i18next";
import clsx from "clsx";
import type { MUISortOptions } from "mui-datatables";

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
    gap: theme.spacing(1),
  },
  notificationTitle: {
    color: theme.palette.primary[600],
    fontWeight: 600,
    fontSize: "13px",
  },
  notificationDescription: {
    color: theme.palette.grey[600],
    fontWeight: 400,
    fontSize: "12px",
  },
  sevIndicator: {
    height: theme.spacing(6),
    width: theme.spacing(0.65),
    borderRadius: theme.shape.borderRadius,
  },
  sevIndicatorSm: {
    height: theme.spacing(2.5),
  },
  sevIndicatorWrapper: {
    display: "flex",
    alignItems: "center",
    gap: theme.spacing(1),
  },
  checkbox: {
    padding: theme.spacing(0.5, 1, 0.5, 0),
  },
}));

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

const NotificationComponent = (classes: ReturnType<typeof useStyles>) => (notification: any) => {
  return (
    <Box className={classes.notificationWrapper}>
      <SevIndicator status={notification.status}>
        <Box className={classes.notificationContent}>
          <Box className={classes.notificationTitle}>{notification.title}</Box>
          <Box className={classes.notificationDescription}>{notification.description}</Box>
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

  const notificationData = useMemo(
    () => [
      {
        notification: {
          title: "CPU Utilization",
          description: "Cluster CPU utilization exceeded 90% for 5 min",
          status: BadgeVariant.Error,
        },
        status: BadgeVariant.Error,
        triggerTime: "May 23, 2:27 PST",
        duration: "12m",
      },
      {
        notification: {
          title: "Free Storage",
          description: "Node free storage is below 25%",
          status: BadgeVariant.Error,
        },
        status: BadgeVariant.Error,
        triggerTime: "May 23, 1:03pm PST",
        duration: "3h 16m",
      },
      {
        notification: {
          title: "YSQL Connection Limit",
          description: "Cluster exceeded 60% YSQL connection limit",
          status: BadgeVariant.Warning,
        },
        status: BadgeVariant.Warning,
        triggerTime: "May 23, 11:45am PST",
        duration: "24m",
      },
    ],
    []
  );

  const filteredNotificationData = useMemo(
    () =>
      notificationData.filter(
        (notification) =>
          (severeFilter && notification.status === BadgeVariant.Error) ||
          (warningFilter && notification.status === BadgeVariant.Warning) ||
          (!severeFilter && !warningFilter)
      ),
    [notificationData, severeFilter, warningFilter]
  );

  const notificationColumns = [
    {
      name: "notification",
      label: t("clusterDetail.alerts.notification.name"),
      options: {
        customBodyRender: NotificationComponent(classes),
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
      customColumnSort: (order: MUISortOptions["direction"]) => {
        return (obj1: { data: any }, obj2: { data: any }) => {
          let val1 = obj1.data.title;
          let val2 = obj2.data.title;
          let compareResult = val2 < val1 ? 1 : val2 == val1 ? 0 : -1;
          return compareResult * (order === "asc" ? 1 : -1);
        };
      },
    },
    {
      name: "status",
      label: t("clusterDetail.alerts.notification.severity"),
      options: {
        customBodyRender: StatusComponent(t),
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "triggerTime",
      label: t("clusterDetail.alerts.notification.triggertime"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "duration",
      label: t("clusterDetail.alerts.notification.duration"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
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
            count: notificationData.length,
          })}
        </Box>
        {notificationData.length ? (
          <Box pb={4} pt={1}>
            <YBTable
              data={filteredNotificationData}
              columns={notificationColumns}
              touchBorder={false}
            />
          </Box>
        ) : (
          <YBLoadingBox>{t("clusterDetail.alerts.notification.nonotification")}</YBLoadingBox>
        )}
      </Box>
    </Box>
  );
};
