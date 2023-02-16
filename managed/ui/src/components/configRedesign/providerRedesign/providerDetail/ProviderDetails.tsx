/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import React, { useState } from 'react';
import { Box, Tab, Tabs, Typography } from '@material-ui/core';
import { ArrowBack } from '@material-ui/icons';
import { useQuery } from 'react-query';
import { browserHistory } from 'react-router';
import { TabContext, TabPanel } from '@material-ui/lab';
import clsx from 'clsx';

import { api, providerQueryKey, universeQueryKey } from '../../../../redesign/helpers/api';
import { YBErrorIndicator, YBLoading } from '../../../common/indicators';
import { ProviderCode, PROVIDER_ROUTE_PREFIX } from '../constants';
import { ProviderOverview } from './ProviderOverview';
import { DropdownButton, MenuItem } from 'react-bootstrap';
import { YBLabelWithIcon } from '../../../common/descriptors';
import { DeleteProviderConfigModal } from '../DeleteProviderConfigModal';
import { UniverseItem, UniverseTable } from './UniverseTable';
import { AWSProviderEditForm } from '../forms/aws/AWSProviderEditForm';
import { AZUProviderEditForm } from '../forms/azu/AZUProviderEditForm';
import { GCPProviderEditForm } from '../forms/gcp/GCPProviderEditForm';
import { assertUnreachableCase } from '../../../../utils/ErrorUtils';
import { K8sProviderEditForm } from '../forms/k8s/K8sProviderEditForm';
import { OnPremProviderEditForm } from '../forms/onPrem/OnPremProviderEditForm';
import { getIntraProviderTab } from '../utils';

import { YBProvider } from '../types';
import { Universe } from '../../../../redesign/helpers/dtos';

import styles from './ProviderDetails.module.scss';

interface ProviderDetailsProps {
  providerUUID: string;
}

const ProviderDetailsTab = {
  OVERVIEW: 'overview',
  CONFIG_DETAILS: 'configDetails',
  UNIVERSES: 'universes'
} as const;
type ProviderDetailsTab = typeof ProviderDetailsTab[keyof typeof ProviderDetailsTab];

const DEFAULT_TAB = ProviderDetailsTab.OVERVIEW;

export const ProviderDetails = ({ providerUUID }: ProviderDetailsProps) => {
  const [currentTab, setCurrentTab] = useState<ProviderDetailsTab>(DEFAULT_TAB);
  const [isDeleteProviderModalOpen, setIsDeleteProviderModalOpen] = useState<boolean>(false);

  const providerQuery = useQuery(providerQueryKey.detail(providerUUID), () =>
    api.fetchProvider(providerUUID)
  );
  const universeListQuery = useQuery(universeQueryKey.ALL, () => api.fetchUniverseList());
  if (
    providerQuery.isLoading ||
    providerQuery.isIdle ||
    universeListQuery.isLoading ||
    universeListQuery.isIdle
  ) {
    return <YBLoading />;
  }

  if (providerQuery.isError) {
    return <YBErrorIndicator customErrorMessage="Error fetching provider." />;
  }
  if (universeListQuery.isError) {
    return <YBErrorIndicator customErrorMessage="Error fetching universe list." />;
  }

  const providerConfig = providerQuery.data;

  const showDeleteProviderModal = () => {
    setIsDeleteProviderModalOpen(true);
  };
  const hideDeleteProviderModal = () => {
    setIsDeleteProviderModalOpen(false);
  };
  const handleTabChange = (event: React.ChangeEvent<{}>, newTab: ProviderDetailsTab) => {
    setCurrentTab(newTab);
  };
  const navigateBack = () => {
    browserHistory.goBack();
  };

  const universeList = universeListQuery.data;
  const linkedUniverses = getLinkedUniverses(providerConfig.uuid, universeList);
  return (
    <div className={styles.viewContainer}>
      <div className={styles.header}>
        <ArrowBack className={styles.arrowBack} fontSize="large" onClick={navigateBack} />
        <Typography variant="h4">{providerConfig.name}</Typography>
        <DropdownButton
          bsClass={clsx(styles.actionButton, 'dropdown')}
          title="Actions"
          id="provider-overview-actions"
          pullRight
        >
          <MenuItem eventKey="1" onClick={showDeleteProviderModal}>
            <YBLabelWithIcon icon="fa fa-trash">Delete Configuration</YBLabelWithIcon>
          </MenuItem>
        </DropdownButton>
      </div>
      <TabContext value={currentTab}>
        <div className={styles.body}>
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
                      <Box
                        height="fit-content"
                        padding="4px 6px"
                        fontSize="10px"
                        borderRadius="6px"
                        bgcolor="#e9eef2"
                      >
                        {linkedUniverses.length}
                      </Box>
                    )}
                  </Box>
                }
                value={ProviderDetailsTab.UNIVERSES}
              />
            </Tabs>
          </div>
          <div className={styles.content}>
            <TabPanel value={ProviderDetailsTab.OVERVIEW}>
              <ProviderOverview
                providerConfig={providerConfig}
                isProviderInUse={linkedUniverses.length > 0}
              />
            </TabPanel>
            <TabPanel value={ProviderDetailsTab.CONFIG_DETAILS}>
              {getProviderConfigDetailsPage(providerConfig)}
            </TabPanel>
            <TabPanel value={ProviderDetailsTab.UNIVERSES}>
              <UniverseTable linkedUniverses={linkedUniverses} />
            </TabPanel>
          </div>
        </div>
      </TabContext>

      <DeleteProviderConfigModal
        open={isDeleteProviderModalOpen}
        onClose={hideDeleteProviderModal}
        providerConfig={providerConfig}
        redirectURL={`/${PROVIDER_ROUTE_PREFIX}/${getIntraProviderTab(providerConfig)}`}
      />
    </div>
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

const getProviderConfigDetailsPage = (providerConfig: YBProvider) => {
  switch (providerConfig.code) {
    case ProviderCode.AWS:
      return <AWSProviderEditForm providerConfig={providerConfig} />;
    case ProviderCode.AZU:
      return <AZUProviderEditForm providerConfig={providerConfig} />;
    case ProviderCode.GCP:
      return <GCPProviderEditForm providerConfig={providerConfig} />;
    case ProviderCode.KUBERNETES:
      return <K8sProviderEditForm providerConfig={providerConfig} />;
    case ProviderCode.ON_PREM:
      return <OnPremProviderEditForm providerConfig={providerConfig} />;
    default:
      return assertUnreachableCase(providerConfig);
  }
};
