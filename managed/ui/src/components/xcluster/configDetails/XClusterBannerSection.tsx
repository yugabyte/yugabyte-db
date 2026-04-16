import { useQuery } from 'react-query';
import moment from 'moment';
import { Trans, useTranslation } from 'react-i18next';
import { makeStyles, Typography } from '@material-ui/core';

import { getAlertConfigurations } from '../../../actions/universe';
import { alertConfigQueryKey, api, metricQueryKey } from '../../../redesign/helpers/api';
import {
  getStrictestReplicationLagAlertThreshold,
  getTableCountsOfConcern
} from '../ReplicationUtils';
import { YBBanner, YBBannerVariant } from '../../common/descriptors';
import {
  hasNecessaryPerm,
  RbacValidator
} from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';
import { YBButton } from '../../common/forms/fields';
import {
  I18N_KEY_PREFIX_XCLUSTER_TERMS,
  liveMetricTimeRangeUnit,
  liveMetricTimeRangeValue,
  MetricName,
  XClusterConfigAction,
  XCLUSTER_REPLICATION_DDL_STEPS_DOCUMENTATION_URL
} from '../constants';

import { AlertTemplate } from '../../../redesign/features/alerts/TemplateComposer/ICustomVariables';
import { XClusterConfig } from '../dtos';
import { NodeAggregation, SplitType } from '../../metrics/dtos';
import { MetricsQueryParams, Universe } from '../../../redesign/helpers/dtos';

interface XClusterBannerSectionProps {
  sourceUniverse: Universe | undefined;
  xClusterConfig: XClusterConfig;
  enabledConfigActions: XClusterConfigAction[];
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
    alignItems: 'center',

    minHeight: '41px'
  },
  bannerActionButtonContainer: {
    display: 'flex',
    gap: theme.spacing(1),

    marginLeft: 'auto'
  }
}));

const TRANSLATION_KEY_PREFIX_XCLUSTER_SHARED_COMPONENT = 'clusterDetail.xCluster.shared';

export const XClusterBannerSection = ({
  sourceUniverse,
  xClusterConfig,
  enabledConfigActions,
  openRestartConfigModal
}: XClusterBannerSectionProps) => {
  const { t } = useTranslation();
  const classes = useStyles();

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
    xClusterConfigUuid: xClusterConfig.uuid,
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
  const xClusterConfigTables = xClusterConfig.tableDetails ?? [];
  const shouldShowTableLagWarning =
    replicationLagAlertConfigQuery.isSuccess &&
    tableReplicationLagQuery.isSuccess &&
    numTablesAboveLagThreshold > 0 &&
    xClusterConfigTables.length > 0;

  const tableCountsOfConcern = getTableCountsOfConcern(xClusterConfig.tableDetails);
  const shouldShowRestartReplicationBanner = tableCountsOfConcern.restartRequired > 0;
  const shouldShowUnableToFetchTableStatusBanner = tableCountsOfConcern.unableToFetchStatus > 0;
  const shouldShowAutoFlagMismatchBanner = tableCountsOfConcern.autoFlagMismatched > 0;
  const shouldShowUnknownErrorBanner = tableCountsOfConcern.unknownError > 0;
  const shouldShowFailedReplicationSetup = tableCountsOfConcern.failedReplicationSetup > 0;
  const shouldShowMismatchedTablesBanner = tableCountsOfConcern.dbSchemaMismatched > 0;
  return (
    <div className={classes.bannerContainer}>
      {shouldShowRestartReplicationBanner && (
        <YBBanner variant={YBBannerVariant.DANGER}>
          <div className={classes.bannerContent}>
            <Typography variant="body2">
              <Trans
                i18nKey={`${TRANSLATION_KEY_PREFIX_XCLUSTER_SHARED_COMPONENT}.banner.tableMissingOperation`}
                components={{
                  bold: <b />
                }}
              />
            </Typography>
            <RbacValidator
              customValidateFunction={() => {
                return (
                  hasNecessaryPerm({
                    ...ApiPermissionMap.MODIFY_XCLUSTER_REPLICATION,
                    onResource: xClusterConfig.sourceUniverseUUID
                  }) &&
                  hasNecessaryPerm({
                    ...ApiPermissionMap.MODIFY_XCLUSTER_REPLICATION,
                    onResource: xClusterConfig.targetUniverseUUID
                  })
                );
              }}
              isControl
            >
              <YBButton
                className={classes.bannerActionButtonContainer}
                btnIcon="fa fa-refresh"
                btnText="Restart Replication"
                onClick={openRestartConfigModal}
                disabled={!enabledConfigActions.includes(XClusterConfigAction.RESTART)}
              />
            </RbacValidator>
          </div>
        </YBBanner>
      )}
      {shouldShowUnableToFetchTableStatusBanner && (
        <YBBanner variant={YBBannerVariant.DANGER}>
          <div className={classes.bannerContent}>
            <Typography variant="body2">
              <Trans
                i18nKey={`${TRANSLATION_KEY_PREFIX_XCLUSTER_SHARED_COMPONENT}.banner.unableToFetchTableStatus`}
                components={{
                  bold: <b />
                }}
                values={{
                  targetUniverseTerm: t('target.xClusterReplication', {
                    keyPrefix: I18N_KEY_PREFIX_XCLUSTER_TERMS
                  })
                }}
              />
            </Typography>
          </div>
        </YBBanner>
      )}
      {shouldShowAutoFlagMismatchBanner && (
        <YBBanner variant={YBBannerVariant.DANGER}>
          <div className={classes.bannerContent}>
            <Typography variant="body2">
              <Trans
                i18nKey={`${TRANSLATION_KEY_PREFIX_XCLUSTER_SHARED_COMPONENT}.banner.autoFlagMismatch`}
                components={{
                  bold: <b />
                }}
                values={{
                  sourceUniverseTerm: t('source.xClusterReplication', {
                    keyPrefix: I18N_KEY_PREFIX_XCLUSTER_TERMS
                  }),
                  targetUniverseTerm: t('target.xClusterReplication', {
                    keyPrefix: I18N_KEY_PREFIX_XCLUSTER_TERMS
                  })
                }}
              />
            </Typography>
          </div>
        </YBBanner>
      )}
      {shouldShowUnknownErrorBanner && (
        <YBBanner variant={YBBannerVariant.DANGER}>
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
        <YBBanner variant={YBBannerVariant.DANGER}>
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
        <YBBanner variant={YBBannerVariant.DANGER}>
          <div className={classes.bannerContent}>
            <Typography variant="body2">
              <Trans
                i18nKey={`${TRANSLATION_KEY_PREFIX_XCLUSTER_SHARED_COMPONENT}.banner.mismatchedTables`}
                components={{
                  bold: <b />,
                  ddlChangeStepsDocsLink: (
                    <a
                      href={XCLUSTER_REPLICATION_DDL_STEPS_DOCUMENTATION_URL}
                      target="_blank"
                      rel="noopener noreferrer"
                    />
                  )
                }}
                values={{
                  sourceUniverseTerm: t('source.xClusterReplication', {
                    keyPrefix: I18N_KEY_PREFIX_XCLUSTER_TERMS
                  }),
                  targetUniverseTerm: t('target.xClusterReplication', {
                    keyPrefix: I18N_KEY_PREFIX_XCLUSTER_TERMS
                  })
                }}
              />
            </Typography>
          </div>
        </YBBanner>
      )}
      {shouldShowTableLagWarning && (
        <YBBanner variant={YBBannerVariant.WARNING}>
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
