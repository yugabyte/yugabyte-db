import { useState } from 'react';
import { TabContext, TabList, TabPanel } from '@material-ui/lab';
import { makeStyles, Tab, useTheme } from '@material-ui/core';
import { useTranslation } from 'react-i18next';

import { ReplicationTables } from '../../configDetails/ReplicationTables';

import { DrConfig } from '../dtos';

interface DrConfigDetailsProps {
  drConfig: DrConfig;
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

export const DrConfigDetails = ({ drConfig }: DrConfigDetailsProps) => {
  const [currentTab, setCurrentTab] = useState<DrConfigTab>(DEFAULT_TAB);
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = useStyles();
  const theme = useTheme();

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
          <Tab label={t('tab.metrics')} value={DrConfigTab.METRICS} />
          <Tab label={t('tab.tables')} value={DrConfigTab.TABLES} />
        </TabList>
        <TabPanel value={DrConfigTab.METRICS}>
          {/* Below component seems to cause rerendering? */}
          {/* <ReplicationContainer
            sourceUniverseUUID={drConfig.xClusterConfig.sourceUniverseUUID}
            hideHeader={true}
            replicationUUID={drConfig.xClusterConfig.uuid}
          /> */}
        </TabPanel>
        <TabPanel value={DrConfigTab.TABLES}>
          <ReplicationTables xClusterConfig={drConfig.xClusterConfig} isDrConfig={true} />
        </TabPanel>
      </TabContext>
    </div>
  );
};
