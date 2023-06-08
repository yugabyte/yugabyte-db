import React, { FC, useMemo } from "react";
import { Box } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBTable, YBLoadingBox, YBToggle } from "@app/components";
import { BadgeVariant, YBBadge } from "@app/components/YBBadge/YBBadge";
import type { TFunction } from "i18next";

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

export const AlertConfigurations: FC = () => {
  const { t } = useTranslation();

  const configurationData = useMemo(
    () => [
      {
        name: "Cluster CPU utilization exceeded 75% for 5 min",
        status: BadgeVariant.Warning,
        enabled: true,
      },
      {
        name: "Cluster CPU utilization exceeded 90% for 5 min",
        status: BadgeVariant.Error,
        enabled: true,
      },
      {
        name: "Cluster storage utilization exceeded 75%",
        status: BadgeVariant.Warning,
        enabled: true,
      },
      {
        name: "Cluster storage utilization exceeds 90%",
        status: BadgeVariant.Error,
        enabled: true,
      },
      {
        name: "Cluster memory utilization exceeded 75% for 10 min",
        status: BadgeVariant.Warning,
        enabled: true,
      },
      {
        name: "Cluster memory utilization exceeded 90% for 10 min",
        status: BadgeVariant.Error,
        enabled: true,
      },
      {
        name: "Cluster exceeded 60% YSQL connection limit",
        status: BadgeVariant.Warning,
        enabled: true,
      },
      {
        name: "Cluster exceeded 85% YSQL connection limit",
        status: BadgeVariant.Error,
        enabled: true,
      },
      {
        name: "Memory utilization exceeded 75% for 5 min",
        status: BadgeVariant.Warning,
        enabled: true,
      },
      {
        name: "Memory utilization exceeded 90% for 5 min",
        status: BadgeVariant.Error,
        enabled: true,
      },
      {
        name: "More than 34% of all primary nodes in the cluster are reporting as down",
        status: BadgeVariant.Warning,
        enabled: true,
      },
      {
        name: "More than 66% of all primary nodes in the cluster are reporting as down",
        status: BadgeVariant.Error,
        enabled: true,
      },
    ],
    []
  );

  const configurationColumns = [
    {
      name: "name",
      label: t("clusterDetail.alerts.configuration.name"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "status",
      label: t("clusterDetail.alerts.configuration.severity"),
      options: {
        customBodyRender: StatusComponent(t),
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "enabled",
      label: t("clusterDetail.alerts.configuration.status"),
      options: {
        customBodyRender: (enabled: boolean) => (
          <YBToggle
            defaultChecked={enabled}
            label={
              enabled
                ? t("clusterDetail.alerts.configuration.enabled")
                : t("clusterDetail.alerts.configuration.disabled")
            }
          />
        ),
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
  ];

  return (
    <Box mt={2}>
      {configurationData.length ? (
        <Box pb={4} pt={1}>
          <YBTable
            data={configurationData}
            columns={configurationColumns}
            options={{ pagination: false }}
            touchBorder={false}
          />
        </Box>
      ) : (
        <YBLoadingBox>{t("clusterDetail.alerts.configuration.noconfigurations")}</YBLoadingBox>
      )}
    </Box>
  );
};
