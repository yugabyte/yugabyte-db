import React, { FC } from 'react';
import { Divider, Box, Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { Link } from 'react-router-dom';

import { YBButton } from '@app/components';
import { useGettingStartedStyles } from './gettingStartedStyles';

import WelcomeCloudImage from '@app/assets/welcome-cloud-image.svg';
import CheckIcon from '@app/assets/check.svg';

const LINK_SIGNUP = 'https://cloud.yugabyte.com/signup?';

// TODO: remove page once PLG feature is stable
export const GettingStarted: FC = () => {
  const classes = useGettingStartedStyles();
  const { t } = useTranslation();

  return (
    <div className={classes.root}>
      <Typography variant="h4" className={classes.headerTitle}>
        {t('managed.signupForManaged')}
      </Typography>
      <Divider />
      <Typography variant="h2" className={classes.getStartedTitle}>
        {t('managed.featureAvailableWith')}
      </Typography>

      <div className={classes.getStartedBlock}>
        <Box pt={4} display="flex">
          <WelcomeCloudImage />
          <Box maxWidth={390} pt={1} pl={5} pr={5}>
            <Box mb={1}>
              <Typography variant="body2">{t('welcome.getStarted.line1')}</Typography>
            </Box>
            <Box mb={3}>
              <Typography variant="body2" color="textSecondary">
                {t('welcome.getStarted.line2')}
              </Typography>
            </Box>
            <YBButton data-testid="BtnAddCluster" variant="gradient" size="large" component={Link}
              to={{ pathname: LINK_SIGNUP }} target="_blank">
              {t('managed.signupForManaged')}
            </YBButton>
          </Box>
        </Box>

        <div className={classes.features}>
          <div className={classes.featureItem}>
            <CheckIcon className={classes.iconCheck} />
            <Box>
              <Box mb={0.5}>
                <Typography variant="body1">{t('welcome.getStarted.feature1Title')}</Typography>
              </Box>
              <Typography variant="body2">{t('welcome.getStarted.feature1Subtitle')}</Typography>
            </Box>
          </div>
          <div className={classes.featureItem}>
            <CheckIcon className={classes.iconCheck} />
            <Box>
              <Box mb={0.5}>
                <Typography variant="body1">{t('welcome.getStarted.feature2Title')}</Typography>
              </Box>
              <Typography variant="body2">{t('welcome.getStarted.feature2Subtitle')}</Typography>
            </Box>
          </div>
          <div className={classes.featureItem}>
            <CheckIcon className={classes.iconCheck} />
            <Box>
              <Box mb={0.5}>
                <Typography variant="body1">{t('welcome.getStarted.feature3Title')}</Typography>
              </Box>
              <Typography variant="body2">{t('welcome.getStarted.feature3Subtitle')}</Typography>
            </Box>
          </div>
          <div className={classes.featureItem}>
            <CheckIcon className={classes.iconCheck} />
            <Box>
              <Box mb={0.5}>
                <Typography variant="body1">{t('welcome.getStarted.feature4Title')}</Typography>
              </Box>
              <Typography variant="body2">{t('welcome.getStarted.feature4Subtitle')}</Typography>
            </Box>
          </div>
        </div>
      </div>
    </div>
  );
};
