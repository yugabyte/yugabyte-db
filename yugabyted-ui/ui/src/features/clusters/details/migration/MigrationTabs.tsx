import React, { ComponentType, FC } from 'react';
import { Box, makeStyles, Tab, Tabs } from '@material-ui/core';
import { generatePath, Link, useRouteMatch } from 'react-router-dom';
import { useTranslation } from 'react-i18next';
import { MigrationOverview } from './MigrationOverview';
import { GettingStarted } from '@app/features/welcome/GettingStarted';

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
  component: ComponentType;
  testId: string;
}

const tabList: ITabListItem[] = [
  {
    name: 'tabMigrations',
    component: MigrationOverview,
    testId: 'MigrationTabList-Migrations'
  },
  {
    name: 'tabMigrationHistory',
    component: GettingStarted,
    testId: 'MigrationTabList-History'
  },
];

export const MigrationTabs: FC = () => {
  const { path, params } = useRouteMatch<App.RouteParams>();
  const classes = useStyles();
  const { t } = useTranslation();

  const [currentTab, setCurrentTab] = React.useState<string>(params.tab ?? tabList[0].name);
  const TabComponent = tabList.find(tab => tab.name === currentTab)?.component;

  const handleChange = (_: React.ChangeEvent<{}>, newValue: string) => {
    setCurrentTab(newValue);
  };

  return (
    <Box>
      <div className={classes.tabSectionContainer}>
        <Tabs indicatorColor="primary" textColor="primary" data-testid="MigrationTabList"
          value={currentTab} onChange={handleChange}>
          {tabList.map((tab) => (
            <Tab
              key={tab.name}
              value={tab.name}
              label={t(`clusterDetail.voyager.${tab.name}`)}
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
        {TabComponent && <TabComponent />}
      </Box>
    </Box>
  );
};
