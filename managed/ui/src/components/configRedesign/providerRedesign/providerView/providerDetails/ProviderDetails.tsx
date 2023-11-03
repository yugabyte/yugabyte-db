/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import { useState } from 'react';
import clsx from 'clsx';
import { Box, Tab, Tabs } from '@material-ui/core';
import { TabContext, TabPanel } from '@material-ui/lab';

import { ManageInstances } from '../instanceTypes/ManageInstances';
import { ProviderCode } from '../../constants';
import { ProviderEditView } from '../../forms/ProviderEditView';
import { ProviderOverview } from './ProviderOverview';
import { UniverseItem, UniverseTable } from './UniverseTable';
import { usePillStyles } from '../../../../../redesign/styles/styles';

import { YBProvider } from '../../types';

import styles from './ProviderDetails.module.scss';

interface ProviderDetailsProps {
  linkedUniverses: UniverseItem[];
  providerConfig: YBProvider;
}

const ProviderDetailsTab = {
  OVERVIEW: 'overview',
  CONFIG_DETAILS: 'configDetails',
  UNIVERSES: 'universes',
  MANAGE_INSTANCES: 'manageInstances'
} as const;
type ProviderDetailsTab = typeof ProviderDetailsTab[keyof typeof ProviderDetailsTab];

const DEFAULT_TAB = ProviderDetailsTab.OVERVIEW;
export const ProviderDetails = ({ linkedUniverses, providerConfig }: ProviderDetailsProps) => {
  const [currentTab, setCurrentTab] = useState<ProviderDetailsTab>(DEFAULT_TAB);
  const classes = usePillStyles();

  const handleTabChange = (_event: React.ChangeEvent<{}>, newTab: ProviderDetailsTab) => {
    setCurrentTab(newTab);
  };
  return (
    <TabContext value={currentTab}>
      <Box display="flex">
        <div className={styles.sideNav}>
          <Tabs
            classes={{ flexContainer: styles.tabGroup, root: styles.noTabBorder }}
            orientation="vertical"
            onChange={handleTabChange}
          >
            <Tab
              classes={{
                root: clsx(
                  styles.tabButton,
                  currentTab === ProviderDetailsTab.OVERVIEW && styles.selectedTab
                ),
                wrapper: styles.tabLabelWrapper
              }}
              label="Overview"
              value={ProviderDetailsTab.OVERVIEW}
            />
            <Tab
              classes={{
                root: clsx(
                  styles.tabButton,
                  currentTab === ProviderDetailsTab.CONFIG_DETAILS && styles.selectedTab
                ),
                wrapper: styles.tabLabelWrapper
              }}
              label="Config Details"
              value={ProviderDetailsTab.CONFIG_DETAILS}
            />
            <Tab
              classes={{
                root: clsx(
                  styles.tabButton,
                  currentTab === ProviderDetailsTab.UNIVERSES && styles.selectedTab
                ),
                wrapper: styles.tabLabelWrapper
              }}
              label={
                <Box display="flex" gridGap="5px">
                  <div>Universes</div>
                  {linkedUniverses.length > 0 && (
                    <div className={classes.pill}>{linkedUniverses.length}</div>
                  )}
                </Box>
              }
              value={ProviderDetailsTab.UNIVERSES}
            />
            {providerConfig.code === ProviderCode.ON_PREM && (
              <Tab
                classes={{
                  root: clsx(
                    styles.tabButton,
                    currentTab === ProviderDetailsTab.MANAGE_INSTANCES && styles.selectedTab
                  ),
                  wrapper: styles.tabLabelWrapper
                }}
                label="Instances"
                value={ProviderDetailsTab.MANAGE_INSTANCES}
              />
            )}
          </Tabs>
        </div>
        <Box width="100%">
          <TabPanel classes={{ root: styles.tabPanel }} value={ProviderDetailsTab.OVERVIEW}>
            <ProviderOverview
              providerConfig={providerConfig}
              isProviderInUse={linkedUniverses.length > 0}
            />
          </TabPanel>
          <TabPanel classes={{ root: styles.tabPanel }} value={ProviderDetailsTab.CONFIG_DETAILS}>
            <ProviderEditView
              providerConfig={providerConfig}
              isProviderInUse={linkedUniverses.length > 0}
            />
          </TabPanel>
          <TabPanel classes={{ root: styles.tabPanel }} value={ProviderDetailsTab.UNIVERSES}>
            <UniverseTable linkedUniverses={linkedUniverses} />
          </TabPanel>
          {providerConfig.code === ProviderCode.ON_PREM && (
            <TabPanel
              classes={{ root: styles.tabPanel }}
              value={ProviderDetailsTab.MANAGE_INSTANCES}
            >
              <ManageInstances providerConfig={providerConfig} />
            </TabPanel>
          )}
        </Box>
      </Box>
    </TabContext>
  );
};
