import { FC, ReactNode, useState } from 'react';
import { makeStyles, Typography } from '@material-ui/core';
import { Trans, useTranslation } from 'react-i18next';
import { useQueryClient } from 'react-query';
import { browserHistory } from 'react-router';
import {
  StyledContent,
  StyledHeader,
  StyledPanel
} from '../../../create-universe/components/DefaultComponents';
import {
  getClusterByType,
  useEditUniverseContext,
  useIsUniverseReady
} from '../../EditUniverseUtils';
import { EditUniverseTabs } from '../../EditUniverseContext';
import { getEditUniverseSettingsRoute } from '../../editUniverseTabUtils';
import { ClusterSpecClusterType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import {
  getGetUniverseQueryKey,
  useConfigureExportTelemetryConfig,
  useGetExportTelemetryConfig
} from '@app/v2/api/universe/universe';
import { taskQueryKey, universeQueryKey } from '@app/redesign/helpers/api';
import { assertUnreachableCase, handleServerError } from '@app/utils/errorHandlingUtils';
import { AuditLogSettingsPanel } from './db-audit-log/AuditLogSettingsPanel';
import { buildDisableTelemetryConfig as buildDisableAuditLogTelemetryConfig } from './db-audit-log/auditLogHelpers';
import { LogConfigCard } from './LogConfigCard';
import { NavigateToTelemetryExportConfirmationModal } from './NavigateToTelemetryExportConfirmationModal';
import { QueryLogSettingsPanel } from './query-log/QueryLogSettingsPanel';
import { buildDisableTelemetryConfig as buildDisableQueryLogTelemetryConfig } from './query-log/queryLogHelpers';
import { TelemetryConfigConfirmationModal } from '../shared/TelemetryConfigConfirmationModal';
import { useExportTelemetryConfigTaskStatus } from '../useExportTelemetryConfigTaskStatus';

import QueryLogIcon from '@app/redesign/assets/approved/query-log.svg';
import AuditLogIcon from '@app/redesign/assets/approved/audit-log.svg';
import IdeaIcon from '@app/redesign/assets/approved/idea.svg';
import InternalLinkIcon from '@app/redesign/assets/approved/internal-link.svg';

const TRANSLATION_KEY_PREFIX = 'editUniverse.logs';
const QUERY_LOG_DOCS_URL =
  'https://docs.yugabyte.com/preview/yugabyte-platform/alerts-monitoring/universe-logging';
const AUDIT_LOG_DOCS_URL =
  'https://docs.yugabyte.com/preview/secure/audit-logging/audit-logging-ysql/';

type DisableConfigType = 'queryLog' | 'auditLog';

const useStyles = makeStyles((theme) => ({
  logsTabContainer: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(3),

    paddingTop: theme.spacing(2),
    width: '100%'
  },
  noteBanner: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1.25),

    width: '100%',
    padding: theme.spacing(2),

    backgroundColor: theme.palette.primary[100],
    border: `1px solid ${theme.palette.grey[200]}`,
    borderRadius: theme.shape.borderRadius
  },
  noteText: {
    color: theme.palette.grey[900],
    fontSize: '13px',
    lineHeight: '16px'
  },
  telemetryExportLink: {
    display: 'inline-flex',
    alignItems: 'center',

    padding: 0,
    border: 'none',
    background: 'none',

    color: theme.palette.primary[600],
    cursor: 'pointer',
    fontFamily: 'inherit',
    fontSize: '13px',
    fontWeight: 500,
    lineHeight: '16px',
    textDecoration: 'none',

    '&:hover': {
      color: theme.palette.primary[600],
      textDecoration: 'none'
    }
  }
}));

interface TelemetryExportTabLinkProps {
  children?: ReactNode;
  className?: string;
  onClick: () => void;
}

const TelemetryExportTabLink: FC<TelemetryExportTabLinkProps> = ({
  children,
  className,
  onClick
}) => (
  <button
    type="button"
    className={className}
    data-testid="LogsTab-TelemetryExportLink"
    onClick={onClick}
  >
    {children}
    <InternalLinkIcon width={24} height={24} />
  </button>
);

export const LogsTab = () => {
  const classes = useStyles();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const queryClient = useQueryClient();
  const { universeData } = useEditUniverseContext();
  const isUniverseReady = useIsUniverseReady();
  const [isAuditLogSettingsModalOpen, setAuditLogSettingsModalOpen] = useState(false);
  const [isQueryLogSettingsModalOpen, setQueryLogSettingsModalOpen] = useState(false);
  const [isNavigateToTelemetryExportModalOpen, setNavigateToTelemetryExportModalOpen] =
    useState(false);
  const [disableConfigType, setDisableConfigType] = useState<DisableConfigType | null>(null);

  const primaryCluster = getClusterByType(universeData!, ClusterSpecClusterType.PRIMARY);
  const universeUuid = universeData?.info?.universe_uuid ?? '';
  const universeName = universeData?.spec?.name ?? '';
  const replicationFactor = primaryCluster?.replication_factor ?? 1;
  const isAuditLogEnabled = primaryCluster?.audit_log_config?.ysql_audit_config?.enabled;
  const isQueryLogEnabled = primaryCluster?.query_log_config?.ysql_query_log_config?.enabled;
  const telemetryExportTabRoute = getEditUniverseSettingsRoute(
    universeUuid,
    EditUniverseTabs.TELEMETRY_EXPORT
  );

  const telemetryConfigQuery = useGetExportTelemetryConfig(universeUuid, undefined, {
    query: { enabled: !!universeUuid }
  });
  const configureTelemetry = useConfigureExportTelemetryConfig();

  const { isTelemetryConfigTaskInProgress, isQueryLogConfiguring, isAuditLogConfiguring } =
    useExportTelemetryConfigTaskStatus(universeUuid);
  const actionDisabled = !isUniverseReady || isTelemetryConfigTaskInProgress;

  const onDisableConfirm = () => {
    if (!disableConfigType) {
      return;
    }

    const configTypeToDisable = disableConfigType;
    let telemetryConfig;
    switch (configTypeToDisable) {
      case 'queryLog':
        telemetryConfig = buildDisableQueryLogTelemetryConfig(telemetryConfigQuery.data);
        break;
      case 'auditLog':
        telemetryConfig = buildDisableAuditLogTelemetryConfig(telemetryConfigQuery.data);
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
              configTypeToDisable === 'queryLog'
                ? t('toast.disableQueryLogRequestFailedLabel')
                : t('toast.disableAuditLogRequestFailedLabel')
          });
          setDisableConfigType(null);
        }
      }
    );
  };

  return (
    <div className={classes.logsTabContainer}>
      <StyledPanel>
        <StyledHeader>{t('troubleshootingLogs')}</StyledHeader>
        <StyledContent>
          {/* eslint-disable-next-line @typescript-eslint/prefer-nullish-coalescing */}
          {isQueryLogEnabled || isQueryLogConfiguring ? (
            <LogConfigCard
              logType="query"
              icon={<QueryLogIcon width={20} height={20} />}
              title={t('databaseQueryLog')}
              logStatus={isQueryLogConfiguring ? 'configuring' : 'active'}
              actionDisabled={actionDisabled}
              actionTestId="LogsTab-EditQueryLoggingButton"
              onEditClick={() => setQueryLogSettingsModalOpen(true)}
              onDisableClick={() => setDisableConfigType('queryLog')}
            />
          ) : (
            <LogConfigCard
              unconfigured
              icon={<QueryLogIcon width={20} height={20} />}
              title={t('databaseQueryLog')}
              description={t('databaseQueryLogDescription')}
              learnMoreUrl={QUERY_LOG_DOCS_URL}
              actionLabel={t('enableQueryLogging')}
              actionDisabled={actionDisabled}
              actionTestId="LogsTab-EnableQueryLoggingButton"
              onActionClick={() => setQueryLogSettingsModalOpen(true)}
            />
          )}
        </StyledContent>
      </StyledPanel>
      <StyledPanel>
        <StyledHeader>{t('complianceLogs')}</StyledHeader>
        <StyledContent>
          {/* eslint-disable-next-line @typescript-eslint/prefer-nullish-coalescing */}
          {isAuditLogEnabled || isAuditLogConfiguring ? (
            <LogConfigCard
              logType="audit"
              icon={<AuditLogIcon width={20} height={20} />}
              title={t('databaseAuditLog')}
              logStatus={isAuditLogConfiguring ? 'configuring' : 'active'}
              actionDisabled={actionDisabled}
              actionTestId="LogsTab-EditAuditLoggingButton"
              onEditClick={() => setAuditLogSettingsModalOpen(true)}
              onDisableClick={() => setDisableConfigType('auditLog')}
            />
          ) : (
            <LogConfigCard
              unconfigured
              icon={<AuditLogIcon width={20} height={20} />}
              title={t('databaseAuditLog')}
              description={t('databaseAuditLogDescription')}
              learnMoreUrl={AUDIT_LOG_DOCS_URL}
              actionLabel={t('enableAuditLogging')}
              actionDisabled={actionDisabled}
              actionTestId="LogsTab-EnableAuditLoggingButton"
              onActionClick={() => setAuditLogSettingsModalOpen(true)}
            />
          )}
        </StyledContent>
      </StyledPanel>
      <div className={classes.noteBanner}>
        <IdeaIcon width={23} height={24} />
        <Typography variant="body2" className={classes.noteText}>
          <Trans
            t={t}
            i18nKey="telemetryExportNote"
            components={{
              bold: <b />,
              telemetryLink: (
                <TelemetryExportTabLink
                  className={classes.telemetryExportLink}
                  onClick={() => setNavigateToTelemetryExportModalOpen(true)}
                />
              )
            }}
          />
        </Typography>
      </div>
      {isAuditLogSettingsModalOpen && universeUuid && (
        <AuditLogSettingsPanel
          open={isAuditLogSettingsModalOpen}
          operation={isAuditLogEnabled ? 'edit' : 'create'}
          universeUuid={universeUuid}
          universeName={universeName}
          replicationFactor={replicationFactor}
          onClose={() => setAuditLogSettingsModalOpen(false)}
        />
      )}
      {isQueryLogSettingsModalOpen && universeUuid && (
        <QueryLogSettingsPanel
          open={isQueryLogSettingsModalOpen}
          operation={isQueryLogEnabled ? 'edit' : 'create'}
          universeUuid={universeUuid}
          universeName={universeName}
          replicationFactor={replicationFactor}
          onClose={() => setQueryLogSettingsModalOpen(false)}
        />
      )}
      {isNavigateToTelemetryExportModalOpen && (
        <NavigateToTelemetryExportConfirmationModal
          onSubmit={() => {
            setNavigateToTelemetryExportModalOpen(false);
            browserHistory.push(telemetryExportTabRoute);
          }}
          modalProps={{
            open: isNavigateToTelemetryExportModalOpen,
            onClose: () => setNavigateToTelemetryExportModalOpen(false)
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
