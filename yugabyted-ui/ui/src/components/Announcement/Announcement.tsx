import React, { FC, useState } from 'react';
import { Backdrop, Box, makeStyles, Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { YBButton } from '@app/components';
import { browserStorage } from '@app/helpers';
import CloudLockedIcon from '@app/assets/cloud-locked.svg';
import MonitoringIcon from '@app/assets/perf-monitoring.svg';
import ActivityIcon from '@app/assets/activity-log.svg';

const useStyles = makeStyles((theme) => ({
  backdrop: {
    zIndex: theme.zIndex.modal
  },
  content: {
    width: 714,
    borderRadius: theme.shape.borderRadius,
    overflow: 'hidden'
  },
  header: {
    padding: theme.spacing(4, 0, 3, 0),
    textAlign: 'center',
    backgroundColor: theme.palette.background.paper,
    '&:before': {
      content: '"\uD83C\uDF89"', // "party popper" unicode symbol
      fontSize: 32,
      marginBottom: theme.spacing(2)
    }
  },
  blueText: {
    color: theme.palette.primary[500]
  },
  body: {
    padding: theme.spacing(3),
    backgroundColor: theme.palette.grey[100]
  },
  card: {
    height: 114,
    display: 'flex',
    alignItems: 'center',
    border: `1px solid ${theme.palette.grey[300]}`,
    borderRadius: theme.shape.borderRadius,
    backgroundColor: theme.palette.background.default,
    marginBottom: theme.spacing(2)
  }
}));

export const Announcement: FC = () => {
  const classes = useStyles();
  const { t } = useTranslation();
  const [visible, setVisible] = useState(!browserStorage.announcementAcknowledged);

  const close = () => {
    setVisible(false);
    browserStorage.announcementAcknowledged = true;
  };

  return (
    <Backdrop open={visible} className={classes.backdrop}>
      <div className={classes.content}>
        <div className={classes.header}>
          <Box mt={2.5} mb={0.5}>
            <Typography variant="h4" color="textSecondary">
              {t('welcome.welcomeToYBCloud')}
            </Typography>
          </Box>
          <Typography variant="h2" className={classes.blueText}>
            {t('announcement.whatsNew')}
          </Typography>
        </div>
        <div className={classes.body}>
          <div className={classes.card}>
            <Box minWidth={32} width={32} mx={4}>
              <CloudLockedIcon />
            </Box>
            <Box pr={4}>
              <Typography variant="h5" gutterBottom>
                {t('announcement.feature1Title')}
              </Typography>
              <Typography variant="body2">{t('announcement.feature1Subtitle')}</Typography>
            </Box>
          </div>
          <div className={classes.card}>
            <Box minWidth={32} width={32} mx={4}>
              <MonitoringIcon />
            </Box>
            <Box pr={4}>
              <Typography variant="h5" gutterBottom>
                {t('announcement.feature2Title')}
              </Typography>
              <Typography variant="body2">{t('announcement.feature2Subtitle')}</Typography>
            </Box>
          </div>
          <div className={classes.card}>
            <Box minWidth={32} width={32} mx={4}>
              <ActivityIcon />
            </Box>
            <Box pr={4}>
              <Typography variant="h5" gutterBottom>
                {t('announcement.feature3Title')}
              </Typography>
              <Typography variant="body2">{t('announcement.feature3Subtitle')}</Typography>
            </Box>
          </div>
          <Box display="flex" justifyContent="center" mt={3}>
            <YBButton data-testid="BtnCloseAnnouncement" variant="gradient" size="large" onClick={close}>
              {t('announcement.gotIt')}
            </YBButton>
          </Box>
        </div>
      </div>
    </Backdrop>
  );
};
