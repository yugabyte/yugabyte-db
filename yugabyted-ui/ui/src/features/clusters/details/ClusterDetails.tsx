import React, { FC, ComponentType } from 'react';
import { useTranslation } from 'react-i18next';
import { generatePath, Link, Route, Switch, useRouteMatch } from 'react-router-dom';
import {
  Tabs,
  Tab,
  makeStyles,
  Box,
} from '@material-ui/core';

// import { OverviewTab } from '@app/features/clusters/details/overview/OverviewTab';
// import { TablesTab } from '@app/features/clusters/details/tables/TablesTab';
import { NodesTab } from '@app/features/clusters/details/nodes/NodesTab';

// import { PerformanceTab } from '@app/features/clusters/details/performance/PerformanceTab';

interface ContextInput {
  state: Record<string, unknown>;
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  dispatch: (action: { type: string; payload?: any }) => void;
}

export const ClusterContext = React.createContext<ContextInput | null>(null);

const useStyles = makeStyles((theme) => ({
  tabSectionContainer: {
    display: 'flex',
    alignItems: 'center',
    width: '100%',
    boxShadow: `inset 0px -1px 0px 0px ${theme.palette.grey[200]}`
  },
  loadingSection: {
    padding: theme.spacing(1.5),
    minHeight: 64,
    border: `1px solid ${theme.palette.grey[200]}`
  },
  loadingHelpDocs: {
    display: 'flex',
    justifyContent: 'flex-start',
    padding: theme.spacing(1.5, 2),
    borderColor: theme.palette.grey[200],
    alignItems: 'center',

    '& > *': {
      marginRight: theme.spacing(3)
    }
  },
  clusterInfo: {
    display: 'flex',
    padding: theme.spacing(2)
  },
  label: {
    color: theme.palette.grey[600],
    fontWeight: 500,
    marginBottom: theme.spacing(0.75),
    textTransform: 'uppercase'
  },
  link: {
    textDecoration: 'none',
    color: theme.palette.text.primary,
    fontWeight: 400,
    '&:hover': {
      textDecoration: 'none'
    }
  },
  region: {
    display: 'flex',
    alignItems: 'center',
    '& > svg': {
      marginRight: theme.spacing(1)
    }
  },
  docsIcon: {
    width: theme.spacing(2),
    height: theme.spacing(2),
    color: theme.palette.grey[600],
    marginRight: theme.spacing(1.5)
  },
  dangerColor: {
    color: theme.palette.error.main
  },
  docLinks: {
    display: 'flex',
    alignItems: 'center'
  },
  resumeBtn: {
    marginLeft: theme.spacing(1.25)
  }
}));

export interface ITabListItem {
  name: string;
  component: ComponentType;
  testId: string;
}

const tabList: ITabListItem[] = [
//   {
//     name: 'tabOverview',
//     component: OverviewTab,
//     testId: 'ClusterTabList-Overview'
//   },
//   {
//     name: 'tabTables',
//     component: TablesTab,
//     testId: 'ClusterTabList-Tables'
//   },
  {
    name: 'tabNodes',
    component: NodesTab,
    testId: 'ClusterTabList-Nodes'
  },
  // {
  //   name: 'tabPerformance',
  //   component: PerformanceTab,
  //   testId: 'ClusterTabList-Performance'
  // },
];

export const ClusterDetails: FC = () => {
  const { path, params } = useRouteMatch<App.RouteParams>();
  const classes = useStyles();
  const { t } = useTranslation();

  return (
    <Box p={2}>
      <div className={classes.tabSectionContainer}>
        <Tabs value={params.tab} indicatorColor="primary" textColor="primary" data-testid="ClusterTabList">
          {tabList.map((tab) => (
            <Tab
              key={tab.name}
              value={tab.name}
              label={t((`clusterDetail.${tab.name}` as unknown) as TemplateStringsArray)}
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
        {/* <Box ml="auto" display="flex" alignItems="center">
          <Box mr={1}>
            <YBButton
              variant="primary"
              startIcon={<BoltIcon />}
              onClick={() => setShowConnectModal(true)}
            >
              {t('clusterDetail.connect')}
            </YBButton>
          </Box>
        </Box> */}
      </div>

      <Box mt={2}>
        <Switch>
          {tabList.map((tab) => (
            <Route key={tab.name} path={`${path}/${tab.name}/:subTab?`} component={tab.component} />
          ))}
        </Switch>
      </Box>
    </Box>
  );
};
