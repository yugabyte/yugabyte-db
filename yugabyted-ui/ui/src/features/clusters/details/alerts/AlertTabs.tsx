import React, { FC } from "react";
import { Box, makeStyles, Tab, Tabs } from "@material-ui/core";
import { generatePath, Link, Route, Switch, useRouteMatch } from "react-router-dom";
import { useTranslation } from "react-i18next";
import type { ITabListItem } from "../ClusterDetails";
import { QueryParamProvider } from "use-query-params";
import { AlertConfigurations } from "./AlertConfigurations";
import { AlertNotificationDetails } from "./AlertNotificationDetails";

const useStyles = makeStyles((theme) => ({
  tabSectionContainer: {
    display: "flex",
    alignItems: "center",
    width: "100%",
    boxShadow: `inset 0px -1px 0px 0px ${theme.palette.grey[200]}`,
  },
}));

const tabList: ITabListItem[] = [
  {
    name: "tabNotifications",
    component: AlertNotificationDetails,
    testId: "ClusterTabList-Notifications",
  },
  {
    name: "tabConfiguration",
    component: AlertConfigurations,
    testId: "ClusterTabList-Configuration",
  },
];

export const AlertTabs: FC = () => {
  const { path, params } = useRouteMatch<App.RouteParams>();
  const classes = useStyles();
  const { t } = useTranslation();

  return (
    <Box>
      <div className={classes.tabSectionContainer}>
        <Tabs
          value={params.tab}
          indicatorColor="primary"
          textColor="primary"
          data-testid="AlertTabList"
        >
          {tabList.map((tab) => (
            <Tab
              key={tab.name}
              value={tab.name}
              label={t(`clusterDetail.alerts.${tab.name}`)}
              component={Link}
              to={generatePath(path, {
                accountId: params.accountId,
                projectId: params.projectId,
                clusterId: params.clusterId,
                tab: tab.name,
              })}
              data-testid={tab.testId}
            />
          ))}
        </Tabs>
      </div>

      <QueryParamProvider ReactRouterRoute={Route}>
        <Switch>
          {tabList.map((tab) => (
            <Route key={tab.name} path={`${path}/${tab.name}`} component={tab.component} />
          ))}
        </Switch>
      </QueryParamProvider>
    </Box>
  );
};
