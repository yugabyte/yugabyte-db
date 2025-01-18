import { useState } from 'react';
import { makeStyles, Tab, Tabs } from '@material-ui/core';
import { TabContext, TabPanel } from '@material-ui/lab';
import { useTranslation } from 'react-i18next';
import clsx from 'clsx';

import { HAReplication } from '../../../components/ha';
import { HaMetrics } from '../../../components/ha/HaMetrics';
import { HAInstancesContainer } from '../../../components/ha/instances/HAInstanceContainer';
import { useLoadHAConfiguration } from '../../../components/ha/hooks/useLoadHAConfiguration';

const useStyles = makeStyles((theme) => ({
  platformHaContainer: {
    display: 'flex',
    gap: theme.spacing(4)
  },
  sideNav: {
    display: 'flex',

    width: '232px'
  },
  activePanelContainer: {
    width: '100%'
  },
  tabGroupRoot: {
    width: '100%',

    borderBottom: 'unset'
  },
  tabGroupFlexContainer: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(2)
  },

  tabButton: {
    padding: `${theme.spacing(1)}px ${theme.spacing(2)}px`,
    width: '100%',
    height: '32px',
    minHeight: 'unset',

    borderRadius: '8px'
  },
  selectedTab: {
    backgroundColor: theme.palette.grey[300]
  },
  tabLabelWrapper: {
    color: theme.palette.grey[900],
    fontSize: '13px',
    fontWeight: 400
  }
}));

const PlatformHaTab = {
  REPLICATION_CONFIGURATION: 'replicationConfiguration',
  INSTANCE_CONFIGURATION: 'instanceConfiguration',
  HA_METRICS: 'haMetrics'
} as const;
type PlatformHaDetailsTab = typeof PlatformHaTab[keyof typeof PlatformHaTab];

const DEFAULT_TAB = PlatformHaTab.REPLICATION_CONFIGURATION;
const TRANSLATION_KEY_PREFIX = 'ha';
export const PlatformHa = () => {
  const [currentTab, setCurrentTab] = useState<PlatformHaDetailsTab>(DEFAULT_TAB);

  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = useStyles();

  const { config, isNoHAConfigExists } = useLoadHAConfiguration({
    loadSchedule: false,
    autoRefresh: true
  });

  const handleTabChange = (_event: React.ChangeEvent<{}>, newTab: PlatformHaDetailsTab) => {
    setCurrentTab(newTab);
  };

  const currentInstance = config?.instances.find((instance) => instance.is_local);
  const shouldShowHaMetricsTab = currentInstance?.is_leader && !isNoHAConfigExists;
  if (!shouldShowHaMetricsTab && currentTab === PlatformHaTab.HA_METRICS) {
    // If the is_leader value changed after a refetch of HA config, then we may
    // need to switch back to "Replication Configuration" tab as the "Metrics"
    // tab will be hidden.
    setCurrentTab(PlatformHaTab.REPLICATION_CONFIGURATION);
  }
  return (
    <TabContext value={currentTab}>
      <div className={classes.platformHaContainer}>
        <div className={classes.sideNav}>
          <Tabs
            classes={{ flexContainer: classes.tabGroupFlexContainer, root: classes.tabGroupRoot }}
            orientation="vertical"
            onChange={handleTabChange}
          >
            <Tab
              classes={{
                root: clsx(
                  classes.tabButton,
                  currentTab === PlatformHaTab.REPLICATION_CONFIGURATION && classes.selectedTab
                ),
                wrapper: classes.tabLabelWrapper
              }}
              label={t('tab.replicationConfiguration')}
              value={PlatformHaTab.REPLICATION_CONFIGURATION}
            />
            <Tab
              classes={{
                root: clsx(
                  classes.tabButton,
                  currentTab === PlatformHaTab.INSTANCE_CONFIGURATION && classes.selectedTab
                ),
                wrapper: classes.tabLabelWrapper
              }}
              label={t('tab.instanceConfiguration')}
              value={PlatformHaTab.INSTANCE_CONFIGURATION}
            />
            {shouldShowHaMetricsTab && (
              <Tab
                classes={{
                  root: clsx(
                    classes.tabButton,
                    currentTab === PlatformHaTab.HA_METRICS && classes.selectedTab
                  ),
                  wrapper: classes.tabLabelWrapper
                }}
                label={t('tab.metrics')}
                value={PlatformHaTab.HA_METRICS}
              />
            )}
          </Tabs>
        </div>
        <div className={classes.activePanelContainer}>
          <TabPanel value={PlatformHaTab.REPLICATION_CONFIGURATION}>
            <HAReplication />
          </TabPanel>
          <TabPanel value={PlatformHaTab.INSTANCE_CONFIGURATION}>
            <HAInstancesContainer />
          </TabPanel>
          {shouldShowHaMetricsTab && (
            <TabPanel value={PlatformHaTab.HA_METRICS}>
              <HaMetrics />
            </TabPanel>
          )}
        </div>
      </div>
    </TabContext>
  );
};
