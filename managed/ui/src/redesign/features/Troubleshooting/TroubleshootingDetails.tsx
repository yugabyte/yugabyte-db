import { useState } from 'react';
import { Tab } from 'react-bootstrap';
import { useSelector } from 'react-redux';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { Box } from '@material-ui/core';
import { TroubleshootConfiguration } from '@yugabytedb/troubleshoot-ui';
import { YBErrorIndicator, YBLoading } from '../../../components/common/indicators';
import { YBTabsPanel } from '../../../components/panels';
import { TroubleshootingConfigInfo } from './TroubleshootingConfigInfo';
import { RegisterTroubleshootingService } from './RegisterTroubleshootingService';
import { QUERY_KEY, TroubleshootingAPI } from './api';
import { isNonEmptyString } from '../../../utils/ObjectUtils';

interface TroubleshootingDetailsProps {
  activeTab: string | undefined;
}

export const ROUTE_PREFIX = 'troubleshoot';

export const ConfigTabKey = {
  CONFIG: 'config',
  UNIVERSES: 'universes'
} as const;
export type ConfigTabKey = typeof ConfigTabKey[keyof typeof ConfigTabKey];

export const TroubleshootingDetails = ({ activeTab }: TroubleshootingDetailsProps) => {
  const { t } = useTranslation();

  const [TpData, setTpData] = useState<any>([]);
  const currentCustomerInfo = useSelector((state: any) => state.customer.currentCustomer.data);
  const tabToDisplay = activeTab ?? ConfigTabKey.CONFIG;

  const TpList = useQuery(QUERY_KEY.fetchTpList, () => TroubleshootingAPI.fetchTpList(), {
    onSuccess: (data) => {
      setTpData(data);
    }
  });

  const onRefetchConfig = () => {
    TpList.refetch();
  };

  if (TpList.isError) {
    return (
      <YBErrorIndicator customErrorMessage={t('clusterDetail.troubleshoot.tpConfigErrorMessage')} />
    );
  }
  if (TpList.isLoading || (TpList.isIdle && TpList.data === undefined)) {
    return <YBLoading />;
  }

  return (
    <Box>
      <YBTabsPanel
        defaultTab={tabToDisplay}
        activeTab={tabToDisplay}
        id="troubleshoot-config-tab-panel"
        routePrefix="/config/troubleshoot/"
      >
        <Tab
          eventKey={ConfigTabKey.CONFIG}
          title={t('clusterDetail.troubleshoot.configTabTitle')}
          key={ConfigTabKey.CONFIG}
          unmountOnExit={true}
        >
          {TpData?.length > 0 ? (
            <TroubleshootingConfigInfo
              metricsUrl={TpData[0].metricsUrl}
              ybaUrl={TpData[0].ybaUrl}
              tpUrl={TpData[0].tpUrl}
              tpUuid={TpData[0].uuid}
              apiToken={TpData[0].apiToken}
              metricsScrapePeriodSecs={TpData[0].metricsScrapePeriodSecs}
              customerUUID={TpData[0].customerUUID}
              inUseStatus={TpData[0].inUseStatus === 'IN_USE'}
              onRefetchConfig={onRefetchConfig}
            />
          ) : (
            <RegisterTroubleshootingService onRefetchConfig={onRefetchConfig} />
          )}
        </Tab>
        <Tab
          eventKey={ConfigTabKey.UNIVERSES}
          title={t('clusterDetail.troubleshoot.universesTabTitle')}
          key={ConfigTabKey.UNIVERSES}
          unmountOnExit={true}
        >
          <TroubleshootConfiguration
            customerUuid={currentCustomerInfo?.uuid}
            apiUrl={isNonEmptyString(TpData?.[0]?.tpUrl) ? `${TpData[0].tpUrl}/api` : ''}
          />
        </Tab>
      </YBTabsPanel>
    </Box>
  );
};
