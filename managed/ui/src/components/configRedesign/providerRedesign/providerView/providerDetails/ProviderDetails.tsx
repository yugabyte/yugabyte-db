/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import React, { useState } from 'react';
import clsx from 'clsx';
import { Box, Tab, Tabs } from '@material-ui/core';
import { TabContext, TabPanel } from '@material-ui/lab';
import { useQuery } from 'react-query';

import { ManageInstances } from '../instanceTypes/ManageInstances';
import { ProviderCode } from '../../constants';
import { ProviderEditView } from '../../forms/ProviderEditView';
import { ProviderOverview } from './ProviderOverview';
import { UniverseItem, UniverseTable } from './UniverseTable';
import { YBErrorIndicator, YBLoading } from '../../../../common/indicators';
import { api, universeQueryKey } from '../../../../../redesign/helpers/api';
import { usePillStyles } from '../../utils';

import { Universe } from '../../../../../redesign/helpers/dtos';
import { YBProvider } from '../../types';

import styles from './ProviderDetails.module.scss';

interface ProviderDetailsProps {
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
export const ProviderDetails = ({ providerConfig }: ProviderDetailsProps) => {
  const [currentTab, setCurrentTab] = useState<ProviderDetailsTab>(DEFAULT_TAB);
  const classes = usePillStyles();

  const universeListQuery = useQuery(universeQueryKey.ALL, () => api.fetchUniverseList());

  if (universeListQuery.isLoading || universeListQuery.isIdle) {
    return <YBLoading />;
  }

  if (universeListQuery.isError) {
    return <YBErrorIndicator customErrorMessage="Error fetching universe list." />;
  }

  const handleTabChange = (_event: React.ChangeEvent<{}>, newTab: ProviderDetailsTab) => {
    setCurrentTab(newTab);
  };
  const universeList = universeListQuery.data;
  const linkedUniverses = getLinkedUniverses(providerConfig.uuid, universeList);
  return (
    <TabContext value={currentTab}>
      <Box display="flex">
        <div className={styles.sideNav}>
          <Tabs
            classes={{ flexContainer: styles.tabGroup }}
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
            <ProviderEditView providerConfig={providerConfig} />
          </TabPanel>
          <TabPanel classes={{ root: styles.tabPanel }} value={ProviderDetailsTab.UNIVERSES}>
            <UniverseTable linkedUniverses={linkedUniverses} />
          </TabPanel>
          {providerConfig.code === ProviderCode.ON_PREM && (
            <TabPanel
              classes={{ root: styles.tabPanel }}
              value={ProviderDetailsTab.MANAGE_INSTANCES}
            >
              <ManageInstances providerUUID={providerConfig.uuid} />
            </TabPanel>
          )}
        </Box>
      </Box>
    </TabContext>
  );
};

const getLinkedUniverses = (providerUUID: string, universes: Universe[]) =>
  universes.reduce((linkedUniverses: UniverseItem[], universe) => {
    const linkedClusters = universe.universeDetails.clusters.filter(
      (cluster) => cluster.userIntent.provider === providerUUID
    );
    if (linkedClusters.length) {
      linkedUniverses.push({
        ...universe,
        linkedClusters: linkedClusters
      });
    }
    return linkedUniverses;
  }, []);
