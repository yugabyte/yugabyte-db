import { FC, useEffect, useMemo } from 'react';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { browserHistory, withRouter, WithRouterProps } from 'react-router';
import { mui, YBTab, YBTabs } from '@yugabyte-ui-library/core';
import { api } from '@app/redesign/helpers/api';
import { useGetUniverse } from '@app/v2/api/universe/universe';
import { ClusterSpecClusterType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { getClusterByType } from './EditUniverseUtils';
import {
  EditUniverseContext,
  EditUniverseTabs,
  InitialEditUniverseContextState
} from './EditUniverseContext';
import { SwitchEditUniverseTabs } from './SwitchEditUniverseTabs';
import { YBLoadingCircleIcon } from '@app/components/common/indicators';
import {
  getEditUniverseSettingsRoute,
  isValidEditUniverseTab,
  parseEditUniverseTabFromPath
} from './editUniverseTabUtils';

const { Grid, styled } = mui;

interface EditUniverseProps {
  universeUUID: string;
}

const TabItem = styled(YBTab)(({ theme }) => ({
  alignItems: 'flex-start'
}));

const EditUniverseComponent: FC<EditUniverseProps & WithRouterProps> = ({
  universeUUID,
  params,
  location
}) => {
  const { t } = useTranslation('translation', { keyPrefix: 'editUniverse.tabs' });

  const pathTab = params?.settingsTab as string | undefined;
  const selectedTab = useMemo(() => parseEditUniverseTabFromPath(pathTab), [pathTab]);

  useEffect(() => {
    if (!universeUUID || !location) return;

    const settingsBasePath = `/universes/${universeUUID}/settings`;
    if (!location.pathname.startsWith(settingsBasePath)) {
      return;
    }

    const isBareSettingsRoute = location.pathname === settingsBasePath;
    if (isBareSettingsRoute || !pathTab || !isValidEditUniverseTab(pathTab)) {
      browserHistory.replace(getEditUniverseSettingsRoute(universeUUID, EditUniverseTabs.GENERAL));
    }
  }, [pathTab, universeUUID, location?.pathname]);

  const handleTabChange = (_event: unknown, newValue: EditUniverseTabs) => {
    if (newValue === selectedTab) return;

    browserHistory.push(getEditUniverseSettingsRoute(universeUUID, newValue));
  };

  const { data: universeData, isLoading, isSuccess } = useGetUniverse(universeUUID);

  const primaryCluster = universeData
    ? getClusterByType(universeData, ClusterSpecClusterType.PRIMARY)
    : undefined;
  const providerUUID = primaryCluster?.provider_spec?.provider;

  const { data: providerRegions, isLoading: isProviderLoading } = useQuery(
    [universeUUID, providerUUID],
    () => api.fetchProviderRegions(providerUUID),
    {
      enabled: isSuccess && !!providerUUID
    }
  );

  const contextValue = useMemo(
    () => ({
      ...InitialEditUniverseContextState,
      activeTab: selectedTab,
      universeData: universeData ?? null,
      providerRegions: providerRegions ?? []
    }),
    [selectedTab, universeData, providerRegions]
  );

  if (isLoading || !universeData || isProviderLoading || !providerRegions) {
    return <YBLoadingCircleIcon />;
  }

  return (
    <Grid container direction="row" spacing={2}>
      <Grid item sx={{ width: '230px' }}>
        <YBTabs
          orientation="vertical"
          value={selectedTab}
          onChange={handleTabChange}
        >
          <TabItem value={EditUniverseTabs.GENERAL} label={t('general')} />
          <TabItem value={EditUniverseTabs.PLACEMENT} label={t('placement')} />
          <TabItem value={EditUniverseTabs.HARDWARE} label={t('hardware')} />
          <TabItem value={EditUniverseTabs.SECURITY} label={t('security')} />
          <TabItem value={EditUniverseTabs.DATABASE} label={t('database')} />
          <TabItem value={EditUniverseTabs.ADVANCED} label={t('advanced')} />
        </YBTabs>
      </Grid>
      <Grid item sx={{ flexGrow: 1, flex: 1 }}>
        <EditUniverseContext.Provider value={contextValue}>
          <SwitchEditUniverseTabs />
        </EditUniverseContext.Provider>
      </Grid>
    </Grid>
  );
};

export const EditUniverse = withRouter(EditUniverseComponent);
