import { FC, useEffect, useMemo } from 'react';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { browserHistory, withRouter, WithRouterProps } from 'react-router';
import { Divider } from '@material-ui/core';
import { mui, YBTab, YBTabs } from '@yugabyte-ui-library/core';

import { YBLoadingCircleIcon } from '@app/components/common/indicators';
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
import {
  getEditUniverseSettingsRoute,
  isValidEditUniverseTab,
  parseEditUniverseTabFromPath
} from './editUniverseTabUtils';
import { WhereThingsMovedTip } from '@app/redesign/features-v2/onboarding/universe-revamp/tips/WhereThingsMovedTip';

const { styled, Box } = mui;

interface EditUniverseProps {
  universeUUID: string;
}

const TabItem = styled(YBTab)(({ theme }) => ({
  alignItems: 'flex-start'
}));

const StyledDivider = styled(Divider)(({ theme }) => ({
  width: '200px',
  marginBottom: theme.spacing(1)
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
    <Box
      data-edit-universe-root
      sx={{
        display: 'flex',
        flex: 1,
        width: '100%',
        minHeight: 0,
        overflow: 'hidden',
        // Contain portaled onboarding tips (e.g. Advanced Placement) within this shell.
        position: 'relative'
      }}
    >
      <Box
        sx={{
          display: 'flex',
          flexDirection: 'column',
          width: '232px',
          minWidth: '232px',
          maxWidth: '232px',
          flexShrink: 0,
          overflowY: 'auto',
          overflowX: 'hidden',
          '& .MuiTabs-root': {
            width: '100%',
            maxWidth: '100%',
            overflow: 'hidden'
          },
          '& .MuiTabs-scroller': {
            overflowX: 'hidden !important',
            overflowY: 'visible !important'
          },
          '& .MuiTabs-flexContainer': {
            width: '100%',
            maxWidth: '100%'
          },
          '& .MuiTab-root': {
            maxWidth: '100%',
            boxSizing: 'border-box'
          }
        }}
      >
        <YBTabs
          orientation="vertical"
          variant="secondary"
          tabWidth={200}
          value={selectedTab}
          onChange={handleTabChange}
        >
          <TabItem value={EditUniverseTabs.GENERAL} label={t('general')} />
          <TabItem value={EditUniverseTabs.PLACEMENT} label={t('placement')} />
          <TabItem value={EditUniverseTabs.HARDWARE} label={t('hardware')} />
          <TabItem value={EditUniverseTabs.SECURITY} label={t('security')} />
          <TabItem value={EditUniverseTabs.DATABASE} label={t('database')} />
          <TabItem value={EditUniverseTabs.ADVANCED} label={t('advanced')} />
          <StyledDivider orientation="horizontal" />
          <TabItem value={EditUniverseTabs.LOGS} label={t('logs')} />
          <TabItem value={EditUniverseTabs.TELEMETRY_EXPORT} label={t('telemetryExport')} />
        </YBTabs>
        <Box
          sx={{ marginTop: '-8px', marginLeft: '16px', maxWidth: 'calc(100% - 16px)', minWidth: 0 }}
        >
          <WhereThingsMovedTip />
        </Box>
      </Box>
      <Box
        sx={{
          display: 'flex',
          flexDirection: 'column',
          flex: 1,
          minHeight: 0,
          minWidth: 0,
          overflow: 'auto'
        }}
      >
        <Box
          sx={{
            display: 'flex',
            flexDirection: 'column',
            minWidth: '856px',
            width: '100%',
            mt: 2
          }}
        >
          <EditUniverseContext.Provider value={contextValue}>
            <SwitchEditUniverseTabs />
          </EditUniverseContext.Provider>
        </Box>
      </Box>
    </Box>
  );
};

export const EditUniverse = withRouter(EditUniverseComponent);
