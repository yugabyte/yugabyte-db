import { makeStyles, Typography } from '@material-ui/core';
import { YBButton as YBLibraryButton } from '@yugabyte-ui-library/core';
import { Trans, useTranslation } from 'react-i18next';
import { browserHistory } from 'react-router';

import { YBButton } from '../../../redesign/components';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';
import { hasNecessaryPerm } from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { Universe } from '../../../redesign/helpers/dtos';
import { YBBanner, YBBannerVariant } from '../../common/descriptors';
import { XClusterConfigAction } from '../constants';
import { getTableCountsOfConcern } from '../ReplicationUtils';
import {
  XClusterTableWarningBanner,
  XCLUSTER_TABLE_WARNING_BANNER_TEST_ID
} from '../sharedComponents/XClusterTableWarningBanner';
import { useNumTablesAboveLagThreshold } from '../sharedComponents/useNumTablesAboveLagThreshold';
import { DrConfigAction } from './constants';

import { DrConfig, DrConfigState } from './dtos';

interface DrBannerSectionProps {
  drConfig: DrConfig;
  sourceUniverse: Universe | undefined;
  enabledDrConfigActions: DrConfigAction[];
  enabledXClusterConfigActions: XClusterConfigAction[];
  openRepairConfigModal: () => void;
  openRestartConfigModal: () => void;
  onViewTables: () => void;
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

export const DrBannerSection = ({
  drConfig,
  sourceUniverse,
  enabledDrConfigActions,
  enabledXClusterConfigActions,
  openRepairConfigModal,
  openRestartConfigModal,
  onViewTables
}: DrBannerSectionProps) => {
  const classes = useStyles();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const numTablesAboveLagThreshold = useNumTablesAboveLagThreshold(
    sourceUniverse,
    drConfig.xclusterConfigUuid
  );

  const xClusterConfigTables = drConfig.tableDetails ?? [];
  const isRepairDrPossible =
    enabledDrConfigActions.includes(DrConfigAction.EDIT_TARGET) ||
    enabledXClusterConfigActions.includes(XClusterConfigAction.RESTART);

  const sourceUniverseUuid = drConfig.primaryUniverseUuid;
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
                disabled={!sourceUniverseUuid}
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
                disabled={!sourceUniverseUuid}
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
                disabled={!sourceUniverseUuid}
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
      ) : (
        // A DR state banner already covers table problems: they are expected while a DR task runs or
        // replication is halted, and fixing them is that banner's action (e.g. Repair DR).
        <XClusterTableWarningBanner
          tableCountsOfConcern={getTableCountsOfConcern(xClusterConfigTables)}
          numTablesAboveLagThreshold={
            xClusterConfigTables.length > 0 ? numTablesAboveLagThreshold : 0
          }
          isDrInterface={true}
          onViewTables={onViewTables}
          restartButton={
            // Not wrapped in RbacValidator: its <div> wrapper is invalid inside the banner
            // description <p>.
            <YBLibraryButton
              variant="secondary"
              size="small"
              onClick={openRestartConfigModal}
              disabled={
                !enabledXClusterConfigActions.includes(XClusterConfigAction.RESTART) ||
                !hasNecessaryPerm({
                  ...ApiPermissionMap.DR_CONFIG_RESTART,
                  onResource: drConfig.primaryUniverseUuid
                }) ||
                !hasNecessaryPerm({
                  ...ApiPermissionMap.DR_CONFIG_RESTART,
                  onResource: drConfig.drReplicaUniverseUuid
                })
              }
              dataTestId={`${XCLUSTER_TABLE_WARNING_BANNER_TEST_ID}-restart`}
            >
              {t('restartReplication', { keyPrefix: 'clusterDetail.xCluster.shared.banner' })}
            </YBLibraryButton>
          }
        />
      )}
    </div>
  );
};
