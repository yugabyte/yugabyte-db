import { useState } from 'react';
import { TabContext, TabList, TabPanel } from '@material-ui/lab';
import { Box, makeStyles, Tab, Typography, useTheme } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { getXClusterConfig } from '../utils';

import { ReplicationTables } from '../../configDetails/ReplicationTables';
import { XClusterMetrics } from '../../sharedComponents/XClusterMetrics/XClusterMetrics';

import { DrConfig, DrConfigState } from '../dtos';

interface DrConfigDetailsProps {
  drConfig: DrConfig;
  // We have two GET dr config calls.
  // The first tells YBA backend to return only what is stored in
  // the YBA DB.
  // The second tells YBA backend to also make rpc calls to source/target
  // in order to fetch the table details.
  isTableInfoIncludedInConfig: boolean;
}

const useStyles = makeStyles((theme) => ({
  drConfigContainer: {
    minHeight: '480px',

    background: theme.palette.common.white,
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    borderRadius: '8px'
  },
  tabList: {
    borderBottom: `1px solid ${theme.palette.grey[200]}`
  }
}));

const DrConfigTab = {
  METRICS: 'metrics',
  TABLES: 'tables'
} as const;
type DrConfigTab = typeof DrConfigTab[keyof typeof DrConfigTab];

const DEFAULT_TAB = DrConfigTab.METRICS;
const TRANSLATION_KEY_PREFIX = 'clusterDetail.disasterRecovery.config';

export const DrConfigDetails = ({
  drConfig,
  isTableInfoIncludedInConfig
}: DrConfigDetailsProps) => {
  const [currentTab, setCurrentTab] = useState<DrConfigTab>(DEFAULT_TAB);
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = useStyles();
  const theme = useTheme();

  const xClusterConfig = getXClusterConfig(drConfig);
  const handleTabChange = (_event: React.ChangeEvent<{}>, newTab: DrConfigTab) => {
    setCurrentTab(newTab);
  };
  return (
    <div className={classes.drConfigContainer}>
      <TabContext value={currentTab}>
        <TabList
          classes={{ root: classes.tabList }}
          TabIndicatorProps={{
            style: { backgroundColor: theme.palette.ybacolors.ybOrangeFocus }
          }}
          onChange={handleTabChange}
          aria-label={t('aria.drConfigTabs')}
        >
          <Tab label={t('tab.metrics.label')} value={DrConfigTab.METRICS} />
          <Tab label={t('tab.tables.label')} value={DrConfigTab.TABLES} />
        </TabList>
        <TabPanel value={DrConfigTab.METRICS}>
          {drConfig.state === DrConfigState.FAILOVER_IN_PROGRESS ? (
            <Box display="flex" justifyContent="center">
              <Typography variant="body1">
                {t('tab.metrics.metricsUnavailableDuringFailover')}
              </Typography>
            </Box>
          ) : (
            <XClusterMetrics xClusterConfig={xClusterConfig} isDrInterface={true} />
          )}
        </TabPanel>
        <TabPanel value={DrConfigTab.TABLES}>
          <ReplicationTables
            xClusterConfig={xClusterConfig}
            isDrInterface={true}
            isTableInfoIncludedInConfig={isTableInfoIncludedInConfig}
            drConfigUuid={drConfig.uuid}
          />
        </TabPanel>
      </TabContext>
    </div>
  );
};
