import { useState } from 'react';
import { Box, Collapse, makeStyles, Typography, useTheme } from '@material-ui/core';
import { Trans, useTranslation } from 'react-i18next';
import { browserHistory } from 'react-router';

import MegaphoneIcon from '../../assets/megaphone.svg';
import { NODE_AGENT_FAQ_DOCS_URL, NODE_AGENT_PREREQ_DOCS_URL } from './constants';
import { YBButton } from '../../components/YBButton/YBButton';
import { YBExternalLink } from '../../components/YBLink/YBExternalLink';
import { getStoredBooleanValue } from '../../helpers/utils';
import { EnableAutomaticNodeAgentInstallationModal } from './EnableAutomaticNodeAgentInstallationModal';

interface InstallNodeAgentReminderBannerProps {
  isAutoNodeAgentInstallationEnabled: boolean;
  hasNodeAgentFailures: boolean;
}

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

const TRANSLATION_KEY_PREFIX = 'dashboard.installNodeAgentReminderBanner';
const IS_BANNER_EXPANDED_LOCAL_STORAGE_KEY = 'isInstallNodeAgentReminderBannerExpanded';
const TEST_COMPONENT_NAME = 'InstallNodeAgentReminderBanner';

export const InstallNodeAgentReminderBanner = ({
  isAutoNodeAgentInstallationEnabled,
  hasNodeAgentFailures
}: InstallNodeAgentReminderBannerProps) => {
  const [isBannerExpanded, setIsBannerExpanded] = useState(() =>
    getStoredBooleanValue(IS_BANNER_EXPANDED_LOCAL_STORAGE_KEY, true)
  );
  const [
    isEnableAutomaticNodeAgentInstallationModalOpen,
    setIsEnableAutomaticNodeAgentInstallationModalOpen
  ] = useState(false);
  const classes = useStyles();
  const theme = useTheme();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  const toggleIsBannerExpanded = () => {
    const newValue = !isBannerExpanded;
    setIsBannerExpanded(newValue);
    localStorage.setItem(IS_BANNER_EXPANDED_LOCAL_STORAGE_KEY, newValue.toString());
  };
  const redirectToNodeAgentPage = () => browserHistory.push('/nodeAgent');
  const shouldShowViewNodeAgentButton = isAutoNodeAgentInstallationEnabled || hasNodeAgentFailures;
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
              <Box display="flex" flexDirection="column" gridGap={theme.spacing(1)}>
                <Typography variant="body2" className={classes.bannerAdditionalText}>
                  {t('nodeAgentMustBeInstallToUpgradeYba')}
                </Typography>
                {isAutoNodeAgentInstallationEnabled && !hasNodeAgentFailures ? (
                  <>
                    <Typography variant="body2">{t('automaticInstallationsInProgress')}</Typography>
                    <Typography variant="body2">{t('manuallyUnregisteredNodeAgent')}</Typography>
                  </>
                ) : (
                  <>
                    {!isAutoNodeAgentInstallationEnabled && (
                      <>
                        <Typography variant="body2">{t('waysToInstallNodeAgent')}</Typography>
                        <Typography variant="body2">
                          {t('howToManuallyInstallNodeAgent')}
                        </Typography>
                      </>
                    )}
                    <Typography variant="body2">{t('manuallyUnregisteredNodeAgent')}</Typography>
                    {hasNodeAgentFailures && (
                      <Typography variant="body2">
                        <Trans
                          i18nKey={`${TRANSLATION_KEY_PREFIX}.someNodeAgentsAreNotFunctional`}
                          components={{
                            nodeAgentPrereqDocsLink: (
                              <YBExternalLink href={NODE_AGENT_PREREQ_DOCS_URL} />
                            )
                          }}
                        />
                      </Typography>
                    )}
                  </>
                )}
                <Trans
                  i18nKey={`${TRANSLATION_KEY_PREFIX}.learnMoreText`}
                  components={{
                    nodeAgentFaqDocsLink: <YBExternalLink href={NODE_AGENT_FAQ_DOCS_URL} />
                  }}
                />
              </Box>
              <Box display="flex" gridGap={theme.spacing(1)} marginLeft="auto">
                {shouldShowViewNodeAgentButton && (
                  <YBButton
                    variant="secondary"
                    data-testid={`${TEST_COMPONENT_NAME}-ViewNodeAgentsButton`}
                    onClick={redirectToNodeAgentPage}
                  >
                    {t('viewNodeAgentsButton')}
                  </YBButton>
                )}
                {!isAutoNodeAgentInstallationEnabled && (
                  <YBButton
                    variant="secondary"
                    data-testid={`${TEST_COMPONENT_NAME}-ViewNodeAgentsButton`}
                    onClick={() => setIsEnableAutomaticNodeAgentInstallationModalOpen(true)}
                  >
                    {t('enableAutomaticNodeAgentInstallationButton')}
                  </YBButton>
                )}
              </Box>
            </Box>
          </Collapse>
        </Box>
      </div>
      {isEnableAutomaticNodeAgentInstallationModalOpen && (
        <EnableAutomaticNodeAgentInstallationModal
          modalProps={{
            open: isEnableAutomaticNodeAgentInstallationModalOpen,
            onClose: () => setIsEnableAutomaticNodeAgentInstallationModalOpen(false)
          }}
        />
      )}
    </div>
  );
};
