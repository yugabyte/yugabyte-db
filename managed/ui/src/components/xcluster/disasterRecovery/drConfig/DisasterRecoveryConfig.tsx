import { useState } from 'react';
import { TabContext, TabList, TabPanel } from '@material-ui/lab';
import { makeStyles, Tab, useTheme } from '@material-ui/core';
import { useTranslation } from 'react-i18next';

import { DrStatus } from './DrStatus';

import { DrConfig } from '../types';
import { DrConfigOverview } from './DrConfigOverview';

interface DisasterRecoveryConfigProps {
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
  OVERVIEW: 'overview',
  DR_STREAM_STATUS: 'drStreamStatus'
} as const;
type DrConfigTab = typeof DrConfigTab[keyof typeof DrConfigTab];

const DEFAULT_TAB = DrConfigTab.OVERVIEW;
const TRANSLATION_KEY_PREFIX = 'clusterDetail.disasterRecovery.config';

export const DisasterRecoveryConfig = ({ drConfig }: DisasterRecoveryConfigProps) => {
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
          <Tab label={t('tab.overview')} value={DrConfigTab.OVERVIEW} />
          <Tab label={t('tab.drStatus')} value={DrConfigTab.DR_STREAM_STATUS} />
        </TabList>
        <TabPanel value={DrConfigTab.OVERVIEW}>
          <DrConfigOverview drConfig={drConfig} />
        </TabPanel>
        <TabPanel value={DrConfigTab.DR_STREAM_STATUS}>
          <DrStatus drConfig={drConfig} />
        </TabPanel>
      </TabContext>
    </div>
  );
};
