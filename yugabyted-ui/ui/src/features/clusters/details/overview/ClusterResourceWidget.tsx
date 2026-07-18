import React, { ComponentType, FC } from 'react';
import { Box, Link, makeStyles, Tab, Tabs } from '@material-ui/core';

import type { ClusterData } from '@app/api/src';
import { DiskUsageGraph } from './DiskUsageGraph';
import { useTranslation } from 'react-i18next';
import { ChevronRight } from '@material-ui/icons';
import { TabContext, TabPanel } from '@material-ui/lab';
import { VCpuUsageResponsiveSankey } from './VCpuUsageResponsiveSankey';
import { Link as RouterLink } from 'react-router-dom';

const useStyles = makeStyles((theme) => ({
  tabSectionContainer: {
    display: 'flex',
    alignItems: 'center',
    width: '100%',
    boxShadow: 'none',
  },
  tabs: {
    '& div': {
      boxShadow: 'none',
    },
    minHeight: "auto",
  },
  tab: {
    padding: 0,
    paddingBottom: theme.spacing(0.4),
    minHeight: "34px",
    marginRight: "20px",
  },
  tabPanel: {
    padding: 0,
  },
  container: {
    justifyContent: 'space-between',
  },
  label: {
    color: theme.palette.grey[600],
    fontWeight: theme.typography.fontWeightMedium as number,
    marginBottom: theme.spacing(0.75),
    textTransform: 'uppercase'
  },
  value: {
    paddingTop: theme.spacing(0.57)
  },
  arrow: {
    color: theme.palette.grey[600],
    marginTop: theme.spacing(0.5)
  },
  link: {
    '&:link, &:focus, &:active, &:visited, &:hover': {
      textDecoration: 'none',
      color: theme.palette.text.primary,
    }
  }
}));

interface ClusterDiskWidgetProps {
  cluster: ClusterData;
}

export interface ITabListItem {
  name: string;
  component: ComponentType<any>;
  testId: string;
}

export const ClusterResourceWidget: FC<ClusterDiskWidgetProps> = ({ cluster }) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const [value, setValue] = React.useState<string>('tabvCpu');

  return (
    <Box minWidth={0}>
      <TabContext value={value}>
        <Box display="flex" alignItems="center">
          <div className={classes.tabSectionContainer}>
            <Tabs value={value} className={classes.tabs} indicatorColor="primary" textColor="primary"
              onChange={(_, n) => setValue(n)} data-testid="vCpuDiskTabList">
              <Tab
                key={'tabvCpu'}
                value={'tabvCpu'}
                className={classes.tab}
                label={t(`clusterDetail.tabvCpu`)}
                data-testid={'vCpuDiskTabList-vCpu'}
              />
              <Tab
                key={'tabDisk'}
                value={'tabDisk'}
                className={classes.tab}
                label={t(`clusterDetail.tabDisk`)}
                data-testid={'vCpuDiskTabList-Disk'}
              />
            </Tabs>
          </div>
          {value === "tabvCpu" &&
            <Link className={classes.link} component={RouterLink} to="/performance/metrics">
              <ChevronRight className={classes.arrow} />
            </Link>
          }
        </Box>
        
        <TabPanel className={classes.tabPanel} value={'tabvCpu'}>
          <VCpuUsageResponsiveSankey cluster={cluster} />
        </TabPanel>
        <TabPanel className={classes.tabPanel} value={'tabDisk'}>
          <DiskUsageGraph cluster={cluster} />
        </TabPanel>
      </TabContext>
    </Box>
  );
};
