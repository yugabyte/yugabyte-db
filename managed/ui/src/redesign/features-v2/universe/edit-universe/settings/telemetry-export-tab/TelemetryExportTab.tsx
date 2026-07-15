import { useMemo, useState } from 'react';
import { makeStyles, Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { useQuery, useQueryClient } from 'react-query';
import { browserHistory } from 'react-router';

import { StyledContent, StyledPanel } from '../../../create-universe/components/DefaultComponents';
import { api, telemetryProviderQueryKey, taskQueryKey, universeQueryKey } from '@app/redesign/helpers/api';
import { assertUnreachableCase, handleServerError } from '@app/utils/errorHandlingUtils';
import { ClusterSpecClusterType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import {
  getGetUniverseQueryKey,
  useConfigureExportTelemetryConfig,
  useGetExportTelemetryConfig
} from '@app/v2/api/universe/universe';
import {
  getClusterByType,
  isKubernetesUniverse,
  useEditUniverseContext,
  useIsUniverseReady
} from '../../EditUniverseUtils';
import { EditUniverseTabs } from '../../EditUniverseContext';
import { getEditUniverseSettingsRoute } from '../../editUniverseTabUtils';
import { TelemetryConfigConfirmationModal } from '../shared/TelemetryConfigConfirmationModal';
import { useExportTelemetryConfigTaskStatus } from '../useExportTelemetryConfigTaskStatus';
import { NavigateToLogsConfirmationModal } from './NavigateToLogsConfirmationModal';
import { TelemetryExportCard } from './TelemetryExportCard';
import { MetricsExportSettingsModal } from './metrics-export/MetricsExportSettingsModal';
import {
  buildDisableTelemetryConfig as buildDisableMetricsExportTelemetryConfig,
  getMetricsExportDisplayInfo,
  isMetricsExportEnabled
} from './metrics-export/metricsExportHelpers';
import { LogExportSettingsModal } from './log-export/LogExportSettingsModal';
import {
  buildDisableTelemetryConfig as buildDisableLogExportTelemetryConfig,
  getLogExportCardTranslationKeyPrefix,
  getLogExportDisplayInfo,
  isLogExportEnabled,
  LogExportType
} from './log-export/logExportHelpers';

import MetricsIcon from '@app/redesign/assets/approved/trend-sparkline.svg';
import QueryLogIcon from '@app/redesign/assets/approved/query-log.svg';
import AuditLogIcon from '@app/redesign/assets/approved/audit-log.svg';

const TRANSLATION_KEY_PREFIX = 'editUniverse.telemetryExport';
const METRICS_EXPORT_CARD_TRANSLATION_KEY_PREFIX = `${TRANSLATION_KEY_PREFIX}.metricsExport`;
const NO_METADATA_FALLBACK = '-';

type DisableConfigType = 'metricsExport' | 'queryLogExport' | 'auditLogExport';

const useStyles = makeStyles((theme) => ({
  telemetryExportTabContainer: {
    display: 'flex',
    flexDirection: 'column',

    paddingTop: theme.spacing(2),
    width: '100%'
  },
  header: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(1),

    padding: theme.spacing(3)
  },
  title: {
    color: theme.palette.grey[900],
    fontSize: '15px',
    fontWeight: 600,
    lineHeight: '20px'
  },
  subtitle: {
    color: theme.palette.grey[700],
    fontSize: '11.5px',
    fontWeight: 400,
    lineHeight: '16px'
  }
}));

export const TelemetryExportTab = () => {
  const classes = useStyles();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const queryClient = useQueryClient();
  const { universeData } = useEditUniverseContext();
  const isUniverseReady = useIsUniverseReady();
  const [isMetricsExportModalOpen, setIsMetricsExportModalOpen] = useState(false);
  const [openLogExportType, setOpenLogExportType] = useState<LogExportType | null>(null);
  const [navigateToLogsLogType, setNavigateToLogsLogType] = useState<LogExportType | null>(null);
  const [disableConfigType, setDisableConfigType] = useState<DisableConfigType | null>(null);

  const universeUuid = universeData?.info?.universe_uuid ?? '';
  const universeName = universeData?.spec?.name ?? '';
  const isKubernetes = universeData ? isKubernetesUniverse(universeData) : false;
  const primaryCluster = universeData
    ? getClusterByType(universeData, ClusterSpecClusterType.PRIMARY)
    : undefined;
  const replicationFactor = primaryCluster?.replication_factor ?? 1;
  const isQueryLogEnabled = !!primaryCluster?.query_log_config?.ysql_query_log_config?.enabled;
  const isAuditLogEnabled = !!primaryCluster?.audit_log_config?.ysql_audit_config?.enabled;
  const logsTabRoute = getEditUniverseSettingsRoute(universeUuid, EditUniverseTabs.LOGS);

  const telemetryConfigQuery = useGetExportTelemetryConfig(universeUuid, undefined, {
    query: { enabled: !!universeUuid }
  });
  const telemetryProvidersQuery = useQuery(telemetryProviderQueryKey.list(), () =>
    api.fetchTelemetryProviderList()
  );
  const configureTelemetry = useConfigureExportTelemetryConfig();

  const isMetricsExportConfigured = isMetricsExportEnabled(telemetryConfigQuery.data);
  const isQueryLogExportConfigured = isLogExportEnabled('query', telemetryConfigQuery.data);
  const isAuditLogExportConfigured = isLogExportEnabled('audit', telemetryConfigQuery.data);

  const {
    isTelemetryConfigTaskInProgress,
    isMetricsExportConfiguring,
    isQueryLogConfiguring,
    isAuditLogConfiguring
  } = useExportTelemetryConfigTaskStatus(universeUuid);
  const actionDisabled = !isUniverseReady || isTelemetryConfigTaskInProgress || isKubernetes;

  const metricsExportDisplayInfo = useMemo(
    () => getMetricsExportDisplayInfo(telemetryConfigQuery.data, telemetryProvidersQuery.data),
    [telemetryConfigQuery.data, telemetryProvidersQuery.data]
  );
  const queryLogExportDisplayInfo = useMemo(
    () => getLogExportDisplayInfo('query', telemetryConfigQuery.data, telemetryProvidersQuery.data),
    [telemetryConfigQuery.data, telemetryProvidersQuery.data]
  );
  const auditLogExportDisplayInfo = useMemo(
    () => getLogExportDisplayInfo('audit', telemetryConfigQuery.data, telemetryProvidersQuery.data),
    [telemetryConfigQuery.data, telemetryProvidersQuery.data]
  );

  const onDisableConfirm = () => {
    if (!disableConfigType) {
      return;
    }

    const configTypeToDisable = disableConfigType;
    let telemetryConfig;
    switch (configTypeToDisable) {
      case 'metricsExport':
        telemetryConfig = buildDisableMetricsExportTelemetryConfig(telemetryConfigQuery.data);
        break;
      case 'queryLogExport':
        telemetryConfig = buildDisableLogExportTelemetryConfig('query', telemetryConfigQuery.data);
        break;
      case 'auditLogExport':
        telemetryConfig = buildDisableLogExportTelemetryConfig('audit', telemetryConfigQuery.data);
        break;
      default:
        return assertUnreachableCase(configTypeToDisable);
    }

    configureTelemetry.mutate(
      {
        uniUUID: universeUuid,
        data: {
          telemetry_config: telemetryConfig
        }
      },
      {
        onSuccess: () => {
          queryClient.invalidateQueries(telemetryConfigQuery.queryKey);
          queryClient.invalidateQueries(universeQueryKey.detailsV2(universeUuid));
          queryClient.invalidateQueries(getGetUniverseQueryKey(universeUuid));
          queryClient.invalidateQueries(taskQueryKey.universe(universeUuid));
          setDisableConfigType(null);
        },
        onError: (error) => {
          handleServerError(error, {
            customErrorLabel:
              configTypeToDisable === 'metricsExport'
                ? t('toast.disableMetricsExportRequestFailedLabel')
                : configTypeToDisable === 'queryLogExport'
                ? t('toast.disableQueryLogExportRequestFailedLabel')
                : t('toast.disableAuditLogExportRequestFailedLabel')
          });
          setDisableConfigType(null);
        }
      }
    );
  };

  return (
    <div className={classes.telemetryExportTabContainer}>
      <StyledPanel>
        <div className={classes.header}>
          <Typography className={classes.title}>{t('title')}</Typography>
          <Typography className={classes.subtitle}>{t('subtitle')}</Typography>
        </div>
        <StyledContent>
          {/* eslint-disable-next-line @typescript-eslint/prefer-nullish-coalescing */}
          {isMetricsExportConfigured || isMetricsExportConfiguring ? (
            <TelemetryExportCard
              translationKeyPrefix={METRICS_EXPORT_CARD_TRANSLATION_KEY_PREFIX}
              icon={<MetricsIcon width={20} height={20} />}
              title={t('metricsExport.title')}
              exportStatus={isMetricsExportConfiguring ? 'configuring' : 'active'}
              exportConfigurationName={
                metricsExportDisplayInfo?.exportConfigurationName ?? NO_METADATA_FALLBACK
              }
              exportingTo={metricsExportDisplayInfo?.exportingTo ?? NO_METADATA_FALLBACK}
              actionDisabled={actionDisabled}
              actionTestId="TelemetryExportTab-EditMetricsExportButton"
              onEditClick={() => setIsMetricsExportModalOpen(true)}
              onDisableClick={() => setDisableConfigType('metricsExport')}
            />
          ) : (
            <TelemetryExportCard
              unconfigured
              translationKeyPrefix={METRICS_EXPORT_CARD_TRANSLATION_KEY_PREFIX}
              icon={<MetricsIcon width={20} height={20} />}
              title={t('metricsExport.title')}
              description={t('metricsExport.description')}
              statusLabel={t('metricsExport.exportOff')}
              actionLabel={t('metricsExport.actionLabel')}
              actionDisabled={actionDisabled}
              actionTestId="TelemetryExportTab-ExportMetricsButton"
              onActionClick={() => setIsMetricsExportModalOpen(true)}
            />
          )}

          {/* eslint-disable-next-line @typescript-eslint/prefer-nullish-coalescing */}
          {isQueryLogExportConfigured || isQueryLogConfiguring ? (
            <TelemetryExportCard
              translationKeyPrefix={getLogExportCardTranslationKeyPrefix('query')}
              icon={<QueryLogIcon width={20} height={20} />}
              title={t('queryLogExport.title')}
              exportStatus={isQueryLogConfiguring ? 'configuring' : 'active'}
              exportConfigurationName={
                queryLogExportDisplayInfo?.exportConfigurationName ?? NO_METADATA_FALLBACK
              }
              exportingTo={queryLogExportDisplayInfo?.exportingTo ?? NO_METADATA_FALLBACK}
              actionDisabled={actionDisabled}
              actionTestId="TelemetryExportTab-EditQueryLogExportButton"
              onEditClick={() => setOpenLogExportType('query')}
              onDisableClick={() => setDisableConfigType('queryLogExport')}
            />
          ) : (
            <TelemetryExportCard
              unconfigured
              translationKeyPrefix={getLogExportCardTranslationKeyPrefix('query')}
              icon={<QueryLogIcon width={20} height={20} />}
              title={t('queryLogExport.title')}
              description={t('queryLogExport.description')}
              statusLabel={t('queryLogExport.exportOff')}
              loggingOff={!isQueryLogEnabled}
              statusHelperText={
                !isQueryLogEnabled ? t('queryLogExport.enableLoggingHelper') : undefined
              }
              actionLabel={
                isQueryLogEnabled
                  ? t('queryLogExport.actionLabel')
                  : t('queryLogExport.enableLoggingAction')
              }
              actionVariant={isQueryLogEnabled ? 'button' : 'link'}
              actionDisabled={actionDisabled}
              actionTestId={
                isQueryLogEnabled
                  ? 'TelemetryExportTab-ExportQueryLogButton'
                  : 'TelemetryExportTab-EnableQueryLogLink'
              }
              onActionClick={
                isQueryLogEnabled
                  ? () => setOpenLogExportType('query')
                  : () => setNavigateToLogsLogType('query')
              }
            />
          )}

          {/* eslint-disable-next-line @typescript-eslint/prefer-nullish-coalescing */}
          {isAuditLogExportConfigured || isAuditLogConfiguring ? (
            <TelemetryExportCard
              translationKeyPrefix={getLogExportCardTranslationKeyPrefix('audit')}
              icon={<AuditLogIcon width={20} height={20} />}
              title={t('auditLogExport.title')}
              exportStatus={isAuditLogConfiguring ? 'configuring' : 'active'}
              exportConfigurationName={
                auditLogExportDisplayInfo?.exportConfigurationName ?? NO_METADATA_FALLBACK
              }
              exportingTo={auditLogExportDisplayInfo?.exportingTo ?? NO_METADATA_FALLBACK}
              actionDisabled={actionDisabled}
              actionTestId="TelemetryExportTab-EditAuditLogExportButton"
              onEditClick={() => setOpenLogExportType('audit')}
              onDisableClick={() => setDisableConfigType('auditLogExport')}
            />
          ) : (
            <TelemetryExportCard
              unconfigured
              translationKeyPrefix={getLogExportCardTranslationKeyPrefix('audit')}
              icon={<AuditLogIcon width={20} height={20} />}
              title={t('auditLogExport.title')}
              description={t('auditLogExport.description')}
              statusLabel={t('auditLogExport.exportOff')}
              loggingOff={!isAuditLogEnabled}
              statusHelperText={
                !isAuditLogEnabled ? t('auditLogExport.enableLoggingHelper') : undefined
              }
              actionLabel={
                isAuditLogEnabled
                  ? t('auditLogExport.actionLabel')
                  : t('auditLogExport.enableLoggingAction')
              }
              actionVariant={isAuditLogEnabled ? 'button' : 'link'}
              actionDisabled={actionDisabled}
              actionTestId={
                isAuditLogEnabled
                  ? 'TelemetryExportTab-ExportAuditLogButton'
                  : 'TelemetryExportTab-EnableAuditLogLink'
              }
              onActionClick={
                isAuditLogEnabled
                  ? () => setOpenLogExportType('audit')
                  : () => setNavigateToLogsLogType('audit')
              }
            />
          )}
        </StyledContent>
      </StyledPanel>

      {isMetricsExportModalOpen && universeUuid && (
        <MetricsExportSettingsModal
          open={isMetricsExportModalOpen}
          operation={isMetricsExportConfigured ? 'edit' : 'create'}
          universeUuid={universeUuid}
          universeName={universeName}
          replicationFactor={replicationFactor}
          onClose={() => setIsMetricsExportModalOpen(false)}
        />
      )}

      {openLogExportType && universeUuid && (
        <LogExportSettingsModal
          open={!!openLogExportType}
          logExportType={openLogExportType}
          operation={
            isLogExportEnabled(openLogExportType, telemetryConfigQuery.data) ? 'edit' : 'create'
          }
          universeUuid={universeUuid}
          universeName={universeName}
          replicationFactor={replicationFactor}
          onClose={() => setOpenLogExportType(null)}
        />
      )}

      {navigateToLogsLogType && (
        <NavigateToLogsConfirmationModal
          logType={navigateToLogsLogType}
          onSubmit={() => {
            setNavigateToLogsLogType(null);
            browserHistory.push(logsTabRoute);
          }}
          modalProps={{
            open: !!navigateToLogsLogType,
            onClose: () => setNavigateToLogsLogType(null)
          }}
        />
      )}

      {disableConfigType && (
        <TelemetryConfigConfirmationModal
          configType={disableConfigType}
          operation="disable"
          universeName={universeName}
          replicationFactor={replicationFactor}
          isSubmitting={configureTelemetry.isLoading}
          onSubmit={onDisableConfirm}
          modalProps={{
            open: !!disableConfigType,
            onClose: () => setDisableConfigType(null)
          }}
        />
      )}
    </div>
  );
};
