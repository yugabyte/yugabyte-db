import { makeStyles, Typography } from '@material-ui/core';
import { Trans, useTranslation } from 'react-i18next';
import { browserHistory } from 'react-router';

import { YBButton } from '../../../redesign/components';
import { YBBanner, YBBannerVariant } from '../../common/descriptors';
import { YBErrorIndicator } from '../../common/indicators';
import { XClusterTableStatus } from '../constants';
import { DrConfig, DrConfigState } from './dtos';

interface DrBannerSectionProps {
  drConfig: DrConfig;
  openRepairConfigModal: () => void;
  openRestartConfigModal: () => void;
}

const useStyles = makeStyles((theme) => ({
  bannerContainer: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(1)
  },
  bannerContent: {
    display: 'flex',
    alignItems: 'center'
  },
  bannerActionButtonContainer: {
    display: 'flex',
    gap: theme.spacing(1),

    marginLeft: 'auto'
  }
}));

const TRANSLATION_KEY_PREFIX = 'clusterDetail.disasterRecovery';

export const DrBannerSection = ({
  drConfig,
  openRepairConfigModal,
  openRestartConfigModal
}: DrBannerSectionProps) => {
  const classes = useStyles();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  const sourceUniverseUuid = drConfig.primaryUniverseUuid;
  if (!sourceUniverseUuid) {
    // We expect the parent compoent to already handle this case.
    return (
      <YBErrorIndicator
        customErrorMessage={t('error.undefinedSourceUniverseUuid', {
          keyPrefix: 'clusterDetail.xCluster'
        })}
      />
    );
  }

  const numTablesRequiringBootstrap = drConfig.tableDetails.reduce(
    (errorCount: number, xClusterTableDetails) => {
      return xClusterTableDetails.status === XClusterTableStatus.ERROR
        ? errorCount + 1
        : errorCount;
    },
    0
  );
  const shouldShowRestartReplicationBanner = numTablesRequiringBootstrap > 0;

  return (
    <div className={classes.bannerContainer}>
      {drConfig.state === DrConfigState.INITIALIZING ? (
        <YBBanner
          variant={YBBannerVariant.INFO}
          bannerIcon={<i className="fa fa-spinner fa-spin" />}
          isFeatureBanner={true}
        >
          <div className={classes.bannerContent}>
            <Typography variant="body2">
              <Trans
                i18nKey={`${TRANSLATION_KEY_PREFIX}.banner.enablingDr`}
                components={{ bold: <b /> }}
              />
            </Typography>
            <div className={classes.bannerActionButtonContainer}>
              <YBButton
                variant="secondary"
                size="large"
                onClick={() => browserHistory.push(`/universes/${sourceUniverseUuid}/tasks`)}
              >
                {t('actionButton.viewTaskDetails')}
              </YBButton>
            </div>
          </div>
        </YBBanner>
      ) : drConfig.state === DrConfigState.FAILOVER_IN_PROGRESS ? (
        <YBBanner
          variant={YBBannerVariant.INFO}
          bannerIcon={<i className="fa fa-spinner fa-spin" />}
          isFeatureBanner={true}
        >
          <div className={classes.bannerContent}>
            <Typography variant="body2">
              <Trans
                i18nKey={`${TRANSLATION_KEY_PREFIX}.banner.failoverInProgress`}
                components={{ bold: <b /> }}
              />
            </Typography>
            <div className={classes.bannerActionButtonContainer}>
              <YBButton
                variant="secondary"
                size="large"
                onClick={() => browserHistory.push(`/universes/${sourceUniverseUuid}/tasks`)}
              >
                {t('actionButton.viewTaskDetails')}
              </YBButton>
            </div>
          </div>
        </YBBanner>
      ) : drConfig.state === DrConfigState.SWITCHOVER_IN_PROGRESS ? (
        <YBBanner
          variant={YBBannerVariant.INFO}
          bannerIcon={<i className="fa fa-spinner fa-spin" />}
          isFeatureBanner={true}
        >
          <div className={classes.bannerContent}>
            <Typography variant="body2">
              <Trans
                i18nKey={`${TRANSLATION_KEY_PREFIX}.banner.switchoverInProgress`}
                components={{ bold: <b /> }}
              />
            </Typography>
            <div className={classes.bannerActionButtonContainer}>
              <YBButton
                variant="secondary"
                size="large"
                onClick={() => browserHistory.push(`/universes/${sourceUniverseUuid}/tasks`)}
              >
                {t('actionButton.viewTaskDetails')}
              </YBButton>
            </div>
          </div>
        </YBBanner>
      ) : drConfig.state === DrConfigState.HALTED ? (
        <YBBanner variant={YBBannerVariant.WARNING} isFeatureBanner={true}>
          <div className={classes.bannerContent}>
            <Typography variant="body2">
              <Trans
                i18nKey={`${TRANSLATION_KEY_PREFIX}.banner.drConfigHalted`}
                components={{ bold: <b /> }}
              />
            </Typography>
            <div className={classes.bannerActionButtonContainer}>
              <YBButton variant="secondary" size="large" onClick={openRepairConfigModal}>
                {t('actionButton.repairDr')}
              </YBButton>
            </div>
          </div>
        </YBBanner>
      ) : null}
      {shouldShowRestartReplicationBanner && (
        <YBBanner variant={YBBannerVariant.DANGER} isFeatureBanner={true}>
          <div className={classes.bannerContent}>
            <Typography variant="body2">
              <Trans
                i18nKey={`${TRANSLATION_KEY_PREFIX}.banner.drConfigRestartRequired`}
                components={{ bold: <b /> }}
              />
            </Typography>
            <div className={classes.bannerActionButtonContainer}>
              <YBButton variant="secondary" size="large" onClick={openRestartConfigModal}>
                {t('actionButton.restartReplication')}
              </YBButton>
            </div>
          </div>
        </YBBanner>
      )}
    </div>
  );
};
