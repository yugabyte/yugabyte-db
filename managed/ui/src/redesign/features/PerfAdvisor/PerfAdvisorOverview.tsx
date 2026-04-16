import { useState } from 'react';
import { Tab } from 'react-bootstrap';
import { useSelector } from 'react-redux';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { Box } from '@material-ui/core';
import { AppName, ConfigureUniverseMetadata } from '@yugabytedb/perf-advisor-ui';
import { YBErrorIndicator, YBLoading } from '../../../components/common/indicators';
import { YBTabsPanel } from '../../../components/panels';
import { PerfAdvisorUniverseConfig } from './PerfAdvisorUniverseConfig';
import { PerfAdvisorRegistration } from './PerfAdvisorRegistration';
import { QUERY_KEY, PerfAdvisorAPI } from './api';
import { isNonEmptyString } from '../../../utils/ObjectUtils';

interface PerfAdvisorOverviewProps {
  activeTab: string | undefined;
}

export const ROUTE_PREFIX = 'troubleshoot';

export const ConfigTabKey = {
  REGISTER: 'register',
  UNIVERSES: 'universes'
} as const;
export type ConfigTabKey = typeof ConfigTabKey[keyof typeof ConfigTabKey];

export const PerfAdvisorOverview = ({ activeTab }: PerfAdvisorOverviewProps) => {
  const { t } = useTranslation();

  const [paData, setPaData] = useState<any>([]);
  const currentCustomerInfo = useSelector((state: any) => state.customer.currentCustomer.data);
  const tabToDisplay = activeTab ?? ConfigTabKey.REGISTER;

  const perfAdvisorUniverseList = useQuery(
    QUERY_KEY.fetchPerfAdvisorList,
    () => PerfAdvisorAPI.fetchPerfAdvisorList(),
    {
      onSuccess: (data) => {
        setPaData(data);
      }
    }
  );

  const onRefetchConfig = () => {
    perfAdvisorUniverseList.refetch();
  };

  if (perfAdvisorUniverseList.isError) {
    return (
      <YBErrorIndicator customErrorMessage={t('clusterDetail.troubleshoot.tpConfigErrorMessage')} />
    );
  }
  if (
    perfAdvisorUniverseList.isLoading ||
    (perfAdvisorUniverseList.isIdle && perfAdvisorUniverseList.data === undefined)
  ) {
    return <YBLoading />;
  }

  return (
    <Box>
      <YBTabsPanel
        defaultTab={tabToDisplay}
        activeTab={tabToDisplay}
        id="troubleshoot-config-tab-panel"
        routePrefix="/config/perfAdvisor/"
      >
        <Tab
          eventKey={ConfigTabKey.REGISTER}
          title={t('clusterDetail.troubleshoot.configTabTitle')}
          key={ConfigTabKey.REGISTER}
          unmountOnExit={true}
        >
          {paData?.length > 0 ? (
            <PerfAdvisorUniverseConfig
              metricsUrl={paData[0].metricsUrl}
              metricsUsername={paData[0].metricsUsername}
              metricsPassword={paData[0].metricsPassword}
              ybaUrl={paData[0].ybaUrl}
              paUrl={paData[0].paUrl}
              paUuid={paData[0].uuid}
              apiToken={paData[0].apiToken}
              tpApiToken={paData[0].tpApiToken}
              metricsScrapePeriodSecs={paData[0].metricsScrapePeriodSecs}
              customerUUID={paData[0].customerUUID}
              inUseStatus={paData[0].inUseStatus === 'IN_USE'}
              onRefetchConfig={onRefetchConfig}
            />
          ) : (
            <PerfAdvisorRegistration onRefetchConfig={onRefetchConfig} />
          )}
        </Tab>
        <Tab
          eventKey={ConfigTabKey.UNIVERSES}
          title={t('clusterDetail.troubleshoot.universesTabTitle')}
          key={ConfigTabKey.UNIVERSES}
          unmountOnExit={true}
        >
          <ConfigureUniverseMetadata
            appName={AppName.YBA}
            customerUuid={currentCustomerInfo?.uuid}
            apiUrl={isNonEmptyString(paData?.[0]?.paUrl) ? `${paData[0].paUrl}/api` : ''}
          />
        </Tab>
      </YBTabsPanel>
    </Box>
  );
};
