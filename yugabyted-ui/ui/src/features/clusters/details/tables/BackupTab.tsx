import React, { FC } from 'react';
import { Box, makeStyles, Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { BackupList } from './BackupList';
import { PitrList } from './PitrList';
import { RestoreList } from './RestoreList';
import { YBButton } from '@app/components';
import clsx from 'clsx';

const useStyles = makeStyles((theme) => ({
  clusterButton: {
    borderRadius: theme.shape.borderRadius,
    marginRight: theme.spacing(1),

    '&:hover': {
      borderColor: theme.palette.grey[300]
    }
  },
  tablesRow: {
    display: 'flex',
    alignItems: 'center',
    margin: theme.spacing(2, 0, 2.5, 0)
  },
  selected: {
    backgroundColor: theme.palette.grey[300],

    '&:hover': {
      backgroundColor: theme.palette.grey[300]
    }
  },
  buttonText: {
    color: theme.palette.text.primary
  },
}));

export interface ITabListItem {
  name: string;
  testId: string;
}

const tabList: ITabListItem[] = [
  {
    name: 'tabBackups',
    testId: 'ClusterTabList-Backups'
  },
  {
    name: 'tabPitr',
    testId: 'ClusterTabList-Pitr'
  },
  {
    name: 'tabRestore',
    testId: 'ClusterTabList-Restore'
  },
];

export const BackupTab: FC = () => {
  const classes = useStyles();
  const { t } = useTranslation();

  const [tab, setTab] = React.useState<string>(tabList[0].name);

  return (
    <Box>
      <Box className={classes.tablesRow}>
        {tabList.map((tabItem) => (
          <YBButton
            key={tabItem.name}
            className={clsx(classes.clusterButton, tab === tabItem.name && classes.selected)}
            onClick={() => setTab(tabItem.name)}
          >
            <Typography variant="body2" className={classes.buttonText}>
              {t(`clusterDetail.databases.${tabItem.name}`)}
            </Typography>
          </YBButton>
        ))}
      </Box>

      <Box mt={2}>
        {tab === 'tabBackups' && <BackupList />}
        {tab === 'tabPitr' && <PitrList />}
        {tab === 'tabRestore' && <RestoreList />}
      </Box>
    </Box>
  );
};
