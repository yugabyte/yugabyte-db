import React, { ComponentType, FC, useEffect } from "react";
import { Box, makeStyles, Tab, Tabs } from "@material-ui/core";
import { OverviewTab } from "@app/features/clusters/details/overview/OverviewTab";
import { NodesTab } from "../nodes/NodesTab";
import { useTranslation } from "react-i18next";
import { ActivityTab } from "../activities/ActivityTab";
import { SettingsTab } from "../settings/SettingsTab";
import { StringParam, useQueryParam, useQueryParams, withDefault } from "use-query-params";
import { YBButton } from "@app/components";
import RefreshIcon from "@app/assets/refresh.svg";
import {
  useGetClusterQuery,
  useGetClusterNodesQuery,
  useGetClusterHealthCheckQuery,
  useGetIsLoadBalancerIdleQuery,
  useGetClusterActivitiesQuery,
  useGetClusterAlertsQuery,
  useGetClusterTabletsQuery,
} from "@app/api/src";

const useStyles = makeStyles((theme) => ({
  tabSectionContainer: {
    display: "flex",
    alignItems: "center",
    width: "100%",
    justifyContent: "space-between",
    boxShadow: `inset 0px -1px 0px 0px ${theme.palette.grey[200]}`,
  },
}));

export interface ITabListItem {
  name: string;
  component: ComponentType;
  testId: string;
}

const tabList: ITabListItem[] = [
  {
    name: "tabOverview",
    component: OverviewTab,
    testId: "ClusterTabList-Overview",
  },
  {
    name: "tabNodes",
    component: NodesTab,
    testId: "ClusterTabList-Nodes",
  },
  {
    name: "tabActivity",
    component: ActivityTab,
    testId: "ClusterTabList-Activity",
  },
  {
    name: "tabSettings",
    component: SettingsTab,
    testId: "ClusterTabList-Settings",
  },
];

export const OverviewDetails: FC = () => {
  const classes = useStyles();
  const { t } = useTranslation();

  const [queryParams, setQueryParams] = useQueryParams({
    tab: withDefault(StringParam, tabList[0].name),
    filter: StringParam,
  });
  const [currentTab, setCurrentTab] = React.useState<string>(queryParams.tab);

  useEffect(() => {
    setCurrentTab(queryParams.tab);
  }, [queryParams.tab]);

  const handleChange = (_: React.ChangeEvent<{}>, newValue: string) => {
    setCurrentTab(newValue);
    setQueryParams({
      tab: newValue,
      filter: newValue === "tabNodes" ? queryParams.filter : undefined,
    });
  };

  const { refetch: refetchNodes } = useGetClusterNodesQuery({ query: { enabled: false } });
  const { refetch: refetchCluster } = useGetClusterQuery({ query: { enabled: false } });
  const { refetch: refetchTablets } = useGetClusterTabletsQuery({ query: { enabled: false } });
  const { refetch: refetchHealth } = useGetClusterHealthCheckQuery({ query: { enabled: false } });
  const { refetch: refetchLoadBalancer } = useGetIsLoadBalancerIdleQuery({
    query: { enabled: false },
  });
  const { refetch: refetchActivities } = useGetClusterActivitiesQuery(
    { activities: "INDEX_BACKFILL", status: "IN_PROGRESS" },
    { query: { enabled: false } }
  );
  const { refetch: refetchAlerts } = useGetClusterAlertsQuery({ query: { enabled: false } });
  const [ refreshChartController, setRefreshChartController ] = useQueryParam<boolean | undefined>("refreshChartController");

  const refetch = () => {
    refetchNodes();
    refetchTablets();
    refetchCluster();
    refetchHealth();
    refetchLoadBalancer();
    refetchActivities();
    refetchAlerts();
    setRefreshChartController(!refreshChartController, "replaceIn");
  }

  const TabComponent = tabList.find((tab) => tab.name === currentTab)?.component;

  return (
    <Box>
      <div className={classes.tabSectionContainer}>
        <Tabs
          indicatorColor="primary"
          textColor="primary"
          data-testid="ClusterTabList"
          value={currentTab}
          onChange={handleChange}
        >
          {tabList.map((tab) => (
            <Tab
              key={tab.name}
              value={tab.name}
              label={t(`clusterDetail.${tab.name}`)}
              data-testid={tab.testId}
            />
          ))}
        </Tabs>
        {currentTab === "tabOverview" && (
          <YBButton variant="ghost" startIcon={<RefreshIcon />} onClick={refetch}>
            {t("clusterDetail.performance.actions.refresh")}
          </YBButton>
        )}
      </div>

      <Box mt={2}>{TabComponent && <TabComponent />}</Box>
    </Box>
  );
};
