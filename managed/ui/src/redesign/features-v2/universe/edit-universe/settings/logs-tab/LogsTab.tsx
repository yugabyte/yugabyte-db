import { useState } from 'react';
import { makeStyles, Typography } from '@material-ui/core';
import { Trans, useTranslation } from 'react-i18next';
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
import { ClusterSpecClusterType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { AuditLogSettingsPanel } from './db-audit-log/AuditLogSettingsPanel';
import { LogConfigCard } from './LogConfigCard';
import { QueryLogSettingsPanel } from './query-log/QueryLogSettingsPanel';
import { useExportTelemetryConfigTaskStatus } from './useExportTelemetryConfigTaskStatus';

import QueryLogIcon from '@app/redesign/assets/approved/query-log.svg';
import AuditLogIcon from '@app/redesign/assets/approved/audit-log.svg';
import IdeaIcon from '@app/redesign/assets/approved/idea.svg';

const TRANSLATION_KEY_PREFIX = 'editUniverse.logs';
const QUERY_LOG_DOCS_URL =
  'https://docs.yugabyte.com/preview/yugabyte-platform/alerts-monitoring/universe-logging';
const AUDIT_LOG_DOCS_URL =
  'https://docs.yugabyte.com/preview/secure/audit-logging/audit-logging-ysql/';

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
  }
}));

export const LogsTab = () => {
  const classes = useStyles();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const { universeData } = useEditUniverseContext();
  const isUniverseReady = useIsUniverseReady();
  const [isAuditLogSettingsModalOpen, setAuditLogSettingsModalOpen] = useState(false);
  const [isQueryLogSettingsModalOpen, setQueryLogSettingsModalOpen] = useState(false);

  const primaryCluster = getClusterByType(universeData!, ClusterSpecClusterType.PRIMARY);
  const universeUuid = universeData?.info?.universe_uuid ?? '';
  const universeName = universeData?.spec?.name ?? '';
  const isAuditLogEnabled = primaryCluster?.audit_log_config?.ysql_audit_config?.enabled;
  const isQueryLogEnabled = primaryCluster?.query_log_config?.ysql_query_log_config?.enabled;

  const { isTelemetryConfigTaskInProgress, isQueryLogConfiguring, isAuditLogConfiguring } =
    useExportTelemetryConfigTaskStatus(universeUuid);
  const actionDisabled = !isUniverseReady || isTelemetryConfigTaskInProgress;

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
        <Typography className={classes.noteText}>
          <Trans t={t} i18nKey="telemetryExportNote" components={{ bold: <b /> }} />
        </Typography>
      </div>
      {isAuditLogSettingsModalOpen && universeUuid && (
        <AuditLogSettingsPanel
          open={isAuditLogSettingsModalOpen}
          operation={isAuditLogEnabled ? 'edit' : 'create'}
          universeUuid={universeUuid}
          universeName={universeName}
          onClose={() => setAuditLogSettingsModalOpen(false)}
        />
      )}
      {isQueryLogSettingsModalOpen && universeUuid && (
        <QueryLogSettingsPanel
          open={isQueryLogSettingsModalOpen}
          operation={isQueryLogEnabled ? 'edit' : 'create'}
          universeUuid={universeUuid}
          universeName={universeName}
          onClose={() => setQueryLogSettingsModalOpen(false)}
        />
      )}
    </div>
  );
};
