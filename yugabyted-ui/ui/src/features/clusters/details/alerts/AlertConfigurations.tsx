import React, { FC, useMemo } from "react";
import { Box } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBTable, YBLoadingBox, YBToggle, YBTooltip } from "@app/components";
import { BadgeVariant, YBBadge } from "@app/components/YBBadge/YBBadge";
import type { TFunction } from "i18next";
import { useLocalStorage } from "react-use";
import { AlertConfiguration, alertConfigurationsKey, alertList } from "./alerts";

const NameComponent = (dataIndex: number) => {
  const { name } = alertList[dataIndex];
  const description = "";

  return (
    <Box display="flex" alignItems="center" gridGap={1}>
      {name}
      {description && <YBTooltip title={description} />}
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

export const AlertConfigurations: FC = () => {
  const { t } = useTranslation();

  const [config, setConfig] = useLocalStorage<AlertConfiguration[]>(alertConfigurationsKey);

  const configurationData = useMemo(
    () =>
      alertList
        .filter((alert) => !alert.hideConfiguration)
        .map((alert) => ({
          ...alert,
          enabled: config?.find((item) => item.key === alert.key)?.enabled ?? true,
        })),
    [config]
  );

  const configurationColumns = [
    {
      name: "name",
      label: t("clusterDetail.alerts.configuration.name"),
      options: {
        customBodyRenderLite: NameComponent,
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
        sort: false,
        customBodyRenderLite: (dataIndex: number) => (
          <YBToggle
            checked={configurationData[dataIndex].enabled}
            label={
              configurationData[dataIndex].enabled
                ? t("clusterDetail.alerts.configuration.enabled")
                : t("clusterDetail.alerts.configuration.disabled")
            }
            onChange={(e) =>
              setConfig(
                configurationData.map((c, index) => ({
                  key: c.key,
                  enabled: index === dataIndex ? e.target.checked : c.enabled,
                }))
              )
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
