import { TablesTab } from '@app/features/clusters/details/tables/TablesTab';
import React, { FC } from 'react';
import { Box, makeStyles, Tab, Tabs } from '@material-ui/core';
import { generatePath, Link, useRouteMatch } from 'react-router-dom';
import { useTranslation } from 'react-i18next';
import { GetClusterTablesApiEnum } from '@app/api/src';

const useStyles = makeStyles((theme) => ({
  tabSectionContainer: {
    display: 'flex',
    alignItems: 'center',
    width: '100%',
    boxShadow: `inset 0px -1px 0px 0px ${theme.palette.grey[200]}`
  },
}));

export interface ITabListItem {
  name: string;
  testId: string;
}

const tabList: ITabListItem[] = [
  {
    name: 'tabYsql',
    testId: 'ClusterTabList-Ysql'
  },
  {
    name: 'tabYcql',
    testId: 'ClusterTabList-Ycql'
  },
];

export const DatabasesDetails: FC = () => {
  const { path, params } = useRouteMatch<App.RouteParams>();
  const classes = useStyles();
  const { t } = useTranslation();

  return (
    <Box>
      <div className={classes.tabSectionContainer}>
        <Tabs value={params.tab} indicatorColor="primary" textColor="primary" data-testid="ClusterTabList">
          {tabList.map((tab) => (
            <Tab
              key={tab.name}
              value={tab.name}
              label={t(`clusterDetail.databases.${tab.name}`)}
              component={Link}
              to={generatePath(path, {
                accountId: params.accountId,
                projectId: params.projectId,
                clusterId: params.clusterId,
                tab: tab.name
              })}
              data-testid={tab.testId}
            />
          ))}
        </Tabs>
      </div>

      <Box mt={2}>
        <TablesTab dbApi={params.tab === 'tabYcql' ? GetClusterTablesApiEnum.Ycql : 
          GetClusterTablesApiEnum.Ysql} />
      </Box>
    </Box>
  );
};
