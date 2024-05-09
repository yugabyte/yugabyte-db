import { YBTabsWithLinksPanel } from '../../../components/panels';
import { Tab } from 'react-bootstrap';
import { isAvailable } from '../../../utils/LayoutUtils';
import { TroubleshootConfiguration } from '@yugabytedb/troubleshoot-ui';
import { useSelector } from 'react-redux';
import { Box, Typography } from '@material-ui/core';
import { TroubleshootingConfigInfo } from './TroubleshootingConfigInfo';

interface TroubleshootingProps {
  params: { tab?: string; section?: string; uuid?: string };
}

export const ROUTE_PREFIX = 'troubleshoot';

export const ConfigTabKey = {
  CONFIG: 'config',
  UNIVERSES: 'universes'
} as const;
export type ConfigTabKey = typeof ConfigTabKey[keyof typeof ConfigTabKey];

export const Troubleshooting = ({ params }: TroubleshootingProps) => {
  const defaultTab = ConfigTabKey.CONFIG;
  const activeTab = params.tab ?? defaultTab;
  const currentCustomerInfo = useSelector((state: any) => state.customer.currentCustomer.data);

  return (
    <Box>
      <h2 className="content-title">{'Troubleshoot'}</h2>
      <YBTabsWithLinksPanel
        defaultTab={defaultTab}
        activeTab={activeTab}
        routePrefix={`/${ROUTE_PREFIX}/`}
        id="config-tab-panel"
        className="universe-detail data-center-config-tab"
      >
        <Tab eventKey={ConfigTabKey.CONFIG} title="Configuration" key="config-troubleshooting">
          <TroubleshootingConfigInfo />
        </Tab>
        <Tab
          eventKey={ConfigTabKey.UNIVERSES}
          title="Universes"
          key="universes-list-troubleshooting"
        >
          <TroubleshootConfiguration customerUuid={currentCustomerInfo?.uuid} />
        </Tab>
      </YBTabsWithLinksPanel>
    </Box>
  );
};
