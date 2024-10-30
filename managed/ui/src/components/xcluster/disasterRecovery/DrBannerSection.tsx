import { makeStyles, Typography } from '@material-ui/core';
import { Trans, useTranslation } from 'react-i18next';
import { browserHistory } from 'react-router';

import { YBButton } from '../../../redesign/components';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';
import { RbacValidator } from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { YBBanner, YBBannerVariant } from '../../common/descriptors';
import { YBErrorIndicator } from '../../common/indicators';
import {
  I18N_KEY_PREFIX_XCLUSTER_TERMS,
  XClusterConfigAction,
  XCLUSTER_DR_DDL_STEPS_DOCUMENTATION_URL
} from '../constants';
import { getTableCountsOfConcern } from '../ReplicationUtils';
import { DrConfigAction } from './constants';

import { DrConfig, DrConfigState } from './dtos';

interface DrBannerSectionProps {
  drConfig: DrConfig;
  enabledDrConfigActions: DrConfigAction[];
  enabledXClusterConfigActions: XClusterConfigAction[];
  openRepairConfigModal: () => void;
  openRestartConfigModal: () => void;
}

const useStyles = makeStyles((theme) => ({
  bannerContainer: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(1),

    marginBottom: theme.spacing(2)
  },
  bannerContent: {
    display: 'flex',
    alignItems: 'center',

    minHeight: '41px'
  },
  bannerActionButtonContainer: {
    display: 'flex',
    gap: theme.spacing(1),

    marginLeft: 'auto'
  }
}));

const TRANSLATION_KEY_PREFIX = 'clusterDetail.disasterRecovery';
const TRANSLATION_KEY_PREFIX_XCLUSTER_SHARED_COMPONENT = 'clusterDetail.xCluster.shared';

export const DrBannerSection = ({
  drConfig,
  enabledDrConfigActions,
  enabledXClusterConfigActions,
  openRepairConfigModal,
  openRestartConfigModal
}: DrBannerSectionProps) => {
  const classes = useStyles();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  const sourceUniverseUuid = drConfig.primaryUniverseUuid;
  if (!sourceUniverseUuid) {
    // We expect the parent component to already handle this case.
    return (
      <YBErrorIndicator
        customErrorMessage={t('error.undefinedSourceUniverseUuid', {
          keyPrefix: 'clusterDetail.xCluster'
        })}
      />
    );
  }

  const tableCountsOfConcern = getTableCountsOfConcern(drConfig.tableDetails);
  const shouldShowRestartReplicationBanner = tableCountsOfConcern.error > 0;

  const shouldShowMismatchedTablesBanner = tableCountsOfConcern.mismatchedTable > 0;
  const isRepairDrPossible =
    enabledDrConfigActions.includes(DrConfigAction.EDIT_TARGET) ||
    enabledXClusterConfigActions.includes(XClusterConfigAction.RESTART);

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
                i18nKey={`${TRANSLATION_KEY_PREFIX}.banner.initializingDr`}
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
              <YBButton
                variant="secondary"
                size="large"
                onClick={openRepairConfigModal}
                disabled={!isRepairDrPossible}
              >
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
              <RbacValidator
                accessRequiredOn={ApiPermissionMap.DR_CONFIG_RESTART}
                overrideStyle={{ display: 'block' }}
                isControl
              >
                <YBButton
                  variant="secondary"
                  size="large"
                  onClick={openRestartConfigModal}
                  disabled={!enabledXClusterConfigActions.includes(XClusterConfigAction.RESTART)}
                >
                  {t('actionButton.restartReplication')}
                </YBButton>
              </RbacValidator>
            </div>
          </div>
        </YBBanner>
      )}
      {shouldShowMismatchedTablesBanner && (
        <YBBanner variant={YBBannerVariant.DANGER} isFeatureBanner={true}>
          <div className={classes.bannerContent}>
            <Typography variant="body2">
              <Trans
                i18nKey={`${TRANSLATION_KEY_PREFIX_XCLUSTER_SHARED_COMPONENT}.banner.mismatchedTables`}
                components={{
                  bold: <b />,
                  ddlChangeStepsDocsLink: (
                    <a
                      href={XCLUSTER_DR_DDL_STEPS_DOCUMENTATION_URL}
                      target="_blank"
                      rel="noopener noreferrer"
                    />
                  )
                }}
                values={{
                  sourceUniverseTerm: t('source.dr', {
                    keyPrefix: I18N_KEY_PREFIX_XCLUSTER_TERMS
                  }),
                  targetUniverseTerm: t('target.dr', {
                    keyPrefix: I18N_KEY_PREFIX_XCLUSTER_TERMS
                  })
                }}
              />
            </Typography>
          </div>
        </YBBanner>
      )}
    </div>
  );
};
