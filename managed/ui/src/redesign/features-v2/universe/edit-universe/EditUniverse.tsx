import { FC, useState } from 'react';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { mui, YBTab, YBTabs } from '@yugabyte-ui-library/core';
import { api } from '@app/redesign/helpers/api';
import { useGetUniverse } from '@app/v2/api/universe/universe';
import {
  EditUniverseContext,
  EditUniverseTabs,
  InitialEditUniverseContextState
} from './EditUniverseContext';
import { SwitchEditUniverseTabs } from './SwitchEditUniverseTabs';
import { YBLoadingCircleIcon } from '@app/components/common/indicators';

const { Grid, styled } = mui;

interface EditUniverseProps {
  universeUUID: string;
}

const TabItem = styled(YBTab)(({ theme }) => ({
  alignItems: 'flex-start'
}));

export const EditUniverse: FC<EditUniverseProps> = ({ universeUUID }) => {
  const { t } = useTranslation('translation', { keyPrefix: 'editUniverse.tabs' });

  const [selectedTab, setSelectedTab] = useState<EditUniverseTabs>(
    InitialEditUniverseContextState.activeTab
  );

  const { data: universeData, isLoading, isSuccess } = useGetUniverse(universeUUID);

  const providerUUID = universeData?.spec?.clusters[0].provider_spec.provider;

  const { data: providerRegions, isLoading: isProviderLoading } = useQuery(
    [universeUUID, providerUUID],
    () => api.fetchProviderRegions(providerUUID),
    {
      enabled: isSuccess && !!providerUUID
    }
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
          onChange={(_event, newValue) => setSelectedTab(newValue)}
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
        <EditUniverseContext.Provider
          value={{
            ...InitialEditUniverseContextState,
            activeTab: selectedTab,
            universeData,
            providerRegions
          }}
        >
          <SwitchEditUniverseTabs />
        </EditUniverseContext.Provider>
      </Grid>
    </Grid>
  );
};
