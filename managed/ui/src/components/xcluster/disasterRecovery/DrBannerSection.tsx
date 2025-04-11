import { makeStyles, Typography } from '@material-ui/core';
import moment from 'moment';
import { Trans, useTranslation } from 'react-i18next';
import { useQuery } from 'react-query';
import { browserHistory } from 'react-router';
import { getAlertConfigurations } from '../../../actions/universe';

import { YBButton } from '../../../redesign/components';
import { AlertTemplate } from '../../../redesign/features/alerts/TemplateComposer/ICustomVariables';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';
import {
  hasNecessaryPerm,
  RbacValidator
} from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { alertConfigQueryKey, api, metricQueryKey } from '../../../redesign/helpers/api';
import { MetricsQueryParams, Universe } from '../../../redesign/helpers/dtos';
import { YBBanner, YBBannerVariant } from '../../common/descriptors';
import { NodeAggregation, SplitType } from '../../metrics/dtos';
import {
  I18N_KEY_PREFIX_XCLUSTER_TERMS,
  liveMetricTimeRangeUnit,
  liveMetricTimeRangeValue,
  MetricName,
  XClusterConfigAction,
  XCLUSTER_DR_DDL_STEPS_DOCUMENTATION_URL
} from '../constants';
import {
  getStrictestReplicationLagAlertThreshold,
  getTableCountsOfConcern
} from '../ReplicationUtils';
import { DrConfigAction } from './constants';

import { DrConfig, DrConfigState } from './dtos';

interface DrBannerSectionProps {
  drConfig: DrConfig;
  sourceUniverse: Universe | undefined;
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
  sourceUniverse,
  enabledDrConfigActions,
  enabledXClusterConfigActions,
  openRepairConfigModal,
  openRestartConfigModal
}: DrBannerSectionProps) => {
  const classes = useStyles();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  const alertConfigFilter = {
    template: AlertTemplate.REPLICATION_LAG,
    targetUuid: sourceUniverse?.universeUUID
  };
  const replicationLagAlertConfigQuery = useQuery(
    alertConfigQueryKey.list(alertConfigFilter),
    () => getAlertConfigurations(alertConfigFilter),
    {
      enabled: !!sourceUniverse
    }
  );

  const replicationLagMetricSettings = {
    metric: MetricName.ASYNC_REPLICATION_SENT_LAG,
    nodeAggregation: NodeAggregation.MAX,
    splitType: SplitType.TABLE
  };
  const replicationLagMetricRequestParams: MetricsQueryParams = {
    metricsWithSettings: [replicationLagMetricSettings],
    nodePrefix: sourceUniverse?.universeDetails.nodePrefix,
    xClusterConfigUuid: drConfig.xclusterConfigUuid,
    start: moment().subtract(liveMetricTimeRangeValue, liveMetricTimeRangeUnit).format('X'),
    end: moment().format('X')
  };
  const tableReplicationLagQuery = useQuery(
    metricQueryKey.live(
      replicationLagMetricRequestParams,
      liveMetricTimeRangeValue,
      liveMetricTimeRangeUnit
    ),
    () => api.fetchMetrics(replicationLagMetricRequestParams),
    {
      enabled: !!sourceUniverse
    }
  );
  let numTablesAboveLagThreshold = 0;
  if (replicationLagAlertConfigQuery.isSuccess && tableReplicationLagQuery.isSuccess) {
    const maxAcceptableLag = getStrictestReplicationLagAlertThreshold(
      replicationLagAlertConfigQuery.data
    );
    tableReplicationLagQuery.data.async_replication_sent_lag?.data.forEach((trace) => {
      if (
        trace.y[trace.y.length - 1] &&
        maxAcceptableLag &&
        trace.y[trace.y.length - 1] > maxAcceptableLag
      ) {
        numTablesAboveLagThreshold += 1;
      }
    });
  }

  const xClusterConfigTables = drConfig.tableDetails ?? [];
  const shouldShowTableLagWarning =
    replicationLagAlertConfigQuery.isSuccess &&
    tableReplicationLagQuery.isSuccess &&
    numTablesAboveLagThreshold > 0 &&
    xClusterConfigTables.length > 0;

  const tableCountsOfConcern = getTableCountsOfConcern(drConfig.tableDetails);
  const shouldShowRestartReplicationBanner = tableCountsOfConcern.restartRequired > 0;
  const shouldShowUnableToFetchTableStatusBanner = tableCountsOfConcern.unableToFetchStatus > 0;
  const shouldShowAutoFlagMismatchBanner = tableCountsOfConcern.autoFlagMismatched > 0;
  const shouldShowUnknownErrorBanner = tableCountsOfConcern.unknownError > 0;
  const shouldShowFailedReplicationSetup = tableCountsOfConcern.failedReplicationSetup > 0;
  const shouldShowMismatchedTablesBanner = tableCountsOfConcern.dbSchemaMismatched > 0;
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
      ) : null}
      {shouldShowRestartReplicationBanner && (
        <YBBanner variant={YBBannerVariant.DANGER} isFeatureBanner={true}>
          <div className={classes.bannerContent}>
            <Typography variant="body2">
              <Trans
                i18nKey={`${TRANSLATION_KEY_PREFIX_XCLUSTER_SHARED_COMPONENT}.banner.tableMissingOperation`}
                components={{ bold: <b /> }}
              />
            </Typography>
            <div className={classes.bannerActionButtonContainer}>
              <RbacValidator
                customValidateFunction={() => {
                  return (
                    hasNecessaryPerm({
                      ...ApiPermissionMap.DR_CONFIG_RESTART,
                      onResource: drConfig.primaryUniverseUuid
                    }) &&
                    hasNecessaryPerm({
                      ...ApiPermissionMap.DR_CONFIG_RESTART,
                      onResource: drConfig.drReplicaUniverseUuid
                    })
                  );
                }}
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
      {shouldShowUnableToFetchTableStatusBanner && (
        <YBBanner variant={YBBannerVariant.DANGER} isFeatureBanner={true}>
          <div className={classes.bannerContent}>
            <Typography variant="body2">
              <Trans
                i18nKey={`${TRANSLATION_KEY_PREFIX_XCLUSTER_SHARED_COMPONENT}.banner.unableToFetchTableStatus`}
                components={{
                  bold: <b />
                }}
                values={{
                  targetUniverseTerm: t('target.dr', {
                    keyPrefix: I18N_KEY_PREFIX_XCLUSTER_TERMS
                  })
                }}
              />
            </Typography>
          </div>
        </YBBanner>
      )}
      {shouldShowAutoFlagMismatchBanner && (
        <YBBanner variant={YBBannerVariant.DANGER} isFeatureBanner={true}>
          <div className={classes.bannerContent}>
            <Typography variant="body2">
              <Trans
                i18nKey={`${TRANSLATION_KEY_PREFIX_XCLUSTER_SHARED_COMPONENT}.banner.autoFlagMismatch`}
                components={{
                  bold: <b />
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
      {shouldShowUnknownErrorBanner && (
        <YBBanner variant={YBBannerVariant.DANGER} isFeatureBanner={true}>
          <div className={classes.bannerContent}>
            <Typography variant="body2">
              <Trans
                i18nKey={`${TRANSLATION_KEY_PREFIX_XCLUSTER_SHARED_COMPONENT}.banner.tableUnknownError`}
                components={{
                  bold: <b />
                }}
              />
            </Typography>
          </div>
        </YBBanner>
      )}
      {shouldShowFailedReplicationSetup && (
        <YBBanner variant={YBBannerVariant.DANGER} isFeatureBanner={true}>
          <div className={classes.bannerContent}>
            <Typography variant="body2">
              <Trans
                i18nKey={`${TRANSLATION_KEY_PREFIX_XCLUSTER_SHARED_COMPONENT}.banner.tableFailedReplicationSetup`}
                components={{
                  bold: <b />
                }}
              />
            </Typography>
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
      {shouldShowTableLagWarning && (
        <YBBanner variant={YBBannerVariant.WARNING} isFeatureBanner={true}>
          <div className={classes.bannerContent}>
            <Typography variant="body2">
              <Trans
                i18nKey={`${TRANSLATION_KEY_PREFIX_XCLUSTER_SHARED_COMPONENT}.banner.tableReplicationLagAlertThresholdExceeded`}
                components={{
                  bold: <b />
                }}
              />
            </Typography>
          </div>
        </YBBanner>
      )}
    </div>
  );
};
