/*
 * Copyright 2024 YugaByte, Inc. and Contributors
 */
import { useState } from 'react';
import clsx from 'clsx';
import { Box, Tab, Tabs, makeStyles } from '@material-ui/core';
import { TabContext, TabPanel } from '@material-ui/lab';

const useStyles = makeStyles((theme) => ({
  sideNav: {
    display: 'flex',
    width: '260px',
    padding: '32px 22px',
    borderRight: '1px solid #E3E3E5'
  },
  tabGroup: {
    display: 'flex',
    flexDirection: 'column',
    gap: '16px',
    paddingLeft: '1px'
  },
  tabButton: {
    padding: '12px 16px',
    width: '210px',
    height: '40px',
    boxSizing: 'border-box',
    borderRadius: '8px'
  },
  selectedTab: {
    margin: '0 -1px',
    backgroundColor: '#F7F7F7',
    border: '1px solid #C8C8C8'
  },
  tabLabelWrapper: {
    alignItems: 'flex-start',
    padding: '12px 16px',
    fontSize: '13px',
    fontWeight: 400,
    textTransform: 'none',
    color: '#232329'
  },
  tabPanel: {
    padding: '32px'
  },
  noTabBorder: {
    borderBottom: 'unset'
  },
  tabBox: {
    border: `1px solid ${theme.palette.grey[300]}`,
    borderRadius: '8px'
  }
}));

const AnomalyDetailsTabs = {
  FIRST_ANOMALY: 'Anomaly 1',
  SECOND_ANOMALY: 'Anomaly 2',
  THIRD_ANOMALY: 'Anomaly 3'
} as const;
// eslint-disable-next-line no-redeclare
type AnomalyDetailsTabs = typeof AnomalyDetailsTabs[keyof typeof AnomalyDetailsTabs];

const DEFAULT_TAB = AnomalyDetailsTabs.FIRST_ANOMALY;
export const AnomalyTabs = () => {
  const classes = useStyles();
  const [currentTab, setCurrentTab] = useState<AnomalyDetailsTabs>(DEFAULT_TAB);
  // const classes = usePillStyles();

  const handleTabChange = (_event: React.ChangeEvent<{}>, newTab: AnomalyDetailsTabs) => {
    setCurrentTab(newTab);
  };
  return (
    <TabContext value={currentTab}>
      <Box display="flex" className={classes.tabBox}>
        <Box className={classes.sideNav}>
          <Tabs
            classes={{ flexContainer: classes.tabGroup, root: classes.noTabBorder }}
            orientation="vertical"
            onChange={handleTabChange}
          >
            <Tab
              classes={{
                root: clsx(
                  classes.tabButton,
                  currentTab === AnomalyDetailsTabs.FIRST_ANOMALY && classes.selectedTab
                ),
                wrapper: classes.tabLabelWrapper
              }}
              label="Anomaly 1"
              value={AnomalyDetailsTabs.FIRST_ANOMALY}
            />
            <Tab
              classes={{
                root: clsx(
                  classes.tabButton,
                  currentTab === AnomalyDetailsTabs.SECOND_ANOMALY && classes.selectedTab
                ),
                wrapper: classes.tabLabelWrapper
              }}
              label="Anomaly 2"
              value={AnomalyDetailsTabs.SECOND_ANOMALY}
            />
            <Tab
              classes={{
                root: clsx(
                  classes.tabButton,
                  currentTab === AnomalyDetailsTabs.THIRD_ANOMALY && classes.selectedTab
                ),
                wrapper: classes.tabLabelWrapper
              }}
              label="Anomaly 3"
              value={AnomalyDetailsTabs.THIRD_ANOMALY}
            />
          </Tabs>
        </Box>
        <Box width="100%">
          <TabPanel classes={{ root: classes.tabPanel }} value={AnomalyDetailsTabs.FIRST_ANOMALY}>
            {/* <ProviderOverview
              providerConfig={providerConfig}
              isProviderInUse={linkedUniverses.length > 0}
            /> */}
          </TabPanel>
          <TabPanel classes={{ root: classes.tabPanel }} value={AnomalyDetailsTabs.SECOND_ANOMALY}>
            {/* <ProviderEditView providerConfig={providerConfig} linkedUniverses={linkedUniverses} /> */}
          </TabPanel>
          <TabPanel classes={{ root: classes.tabPanel }} value={AnomalyDetailsTabs.THIRD_ANOMALY}>
            {/* <UniverseTable linkedUniverses={linkedUniverses} providerConfig={providerConfig} /> */}
          </TabPanel>
        </Box>
      </Box>
    </TabContext>
  );
};
