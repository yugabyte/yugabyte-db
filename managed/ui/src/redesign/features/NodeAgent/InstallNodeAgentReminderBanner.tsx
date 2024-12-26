import { useState } from 'react';
import { Box, Collapse, makeStyles, Typography, useTheme, withWidth } from '@material-ui/core';
import { Trans, useTranslation } from 'react-i18next';
import { browserHistory } from 'react-router';

import { ReactComponent as MegaphoneIcon } from '../../assets/megaphone.svg';
import { NODE_AGENT_PREREQ_DOCS_URL } from './constants';
import { YBButton } from '../../components/YBButton/YBButton';
import { YBExternalLink } from '../../components/YBLink/YBExternalLink';
import { getStoredBooleanValue } from '../../helpers/utils';

const useStyles = makeStyles((theme) => ({
  banner: {
    display: 'flex',

    margin: theme.spacing(3, 0),
    padding: theme.spacing(2, 2),

    color: theme.palette.grey[700],
    backgroundColor: theme.palette.grey[100],
    borderRadius: '8px',
    border: `1px solid ${theme.palette.grey[300]}`
  },
  bannerAdditionalText: {
    '& p': {
      marginBottom: theme.spacing(2)
    },
    '& p:last-child': {
      marginBottom: 0
    }
  },
  grid: {
    display: 'grid',
    gridTemplateColumns: 'auto 1fr',
    gap: theme.spacing(2),
    alignItems: 'start',

    width: '100%'
  },
  reminderLabel: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',

    height: 'fit-content',
    padding: theme.spacing(0.5, 0.75),

    borderRadius: '6px',
    color: theme.palette.grey[900],
    backgroundColor: theme.palette.warning[300]
  },
  expandCollapseButton: {
    marginLeft: 'auto',

    cursor: 'pointer'
  },
  expandCollapseIcon: {
    display: 'flex',
    gap: theme.spacing(1),
    alignItems: 'center'
  },
  megaphoneIcon: {
    width: 18,
    marginRight: theme.spacing(0.5)
  }
}));

const TRANSLATION_KEY_PREFIX = 'dashboard.nodeAgentReminderBanner';
const IS_BANNER_EXPANDED_LOCAL_STORAGE_KEY = 'isInstallNodeAgentReminderBannerExpanded';

export const InstallNodeAgentReminderBanner = () => {
  const [isBannerExpanded, setIsBannerExpanded] = useState(() =>
    getStoredBooleanValue(IS_BANNER_EXPANDED_LOCAL_STORAGE_KEY, true)
  );
  const classes = useStyles();
  const theme = useTheme();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  const toggleIsBannerExpanded = () => {
    const newValue = !isBannerExpanded;
    setIsBannerExpanded(newValue);
    localStorage.setItem(IS_BANNER_EXPANDED_LOCAL_STORAGE_KEY, newValue.toString());
  };
  const redirectToNodeAgentPage = () => browserHistory.push('/nodeAgent');
  return (
    <div className={classes.banner}>
      <div className={classes.grid}>
        <div className={classes.reminderLabel}>
          <MegaphoneIcon className={classes.megaphoneIcon} />
          <Typography variant="button">{t('reminder', { keyPrefix: 'common' })}</Typography>
        </div>
        <Box display="flex" flexDirection="column">
          <Box display="flex" alignItems="center" height={26}>
            <Typography variant="body1">{t('primaryText')}</Typography>
            <Typography
              variant="body2"
              className={classes.expandCollapseButton}
              onClick={toggleIsBannerExpanded}
            >
              {isBannerExpanded ? (
                <Box className={classes.expandCollapseIcon}>
                  <i className="fa fa-angle-up" aria-hidden="true" />
                  {t('collapse', { keyPrefix: 'common' })}
                </Box>
              ) : (
                <Box className={classes.expandCollapseIcon}>
                  <i className="fa fa-angle-down" aria-hidden="true" />
                  {t('expand', { keyPrefix: 'common' })}
                </Box>
              )}
            </Typography>
          </Box>
          <Collapse in={isBannerExpanded}>
            <Box display="flex" flexDirection="column" gridGap={theme.spacing(1)} marginTop={1}>
              <Typography variant="body2" className={classes.bannerAdditionalText}>
                <Trans
                  i18nKey={`${TRANSLATION_KEY_PREFIX}.additionalDetailText`}
                  components={{
                    nodeAgentPrereqDocsLink: <YBExternalLink href={NODE_AGENT_PREREQ_DOCS_URL} />,
                    paragraph: <p />
                  }}
                />
              </Typography>
              <Box marginLeft="auto">
                <YBButton
                  variant="secondary"
                  data-testid="NodeAgentReminderBanner-ReinstallNodeAgentButton"
                  onClick={redirectToNodeAgentPage}
                >
                  {t('reinstallNodeAgentButton')}
                </YBButton>
              </Box>
            </Box>
          </Collapse>
        </Box>
      </div>
    </div>
  );
};
