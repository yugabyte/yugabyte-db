import { useState } from 'react';
import { Box, makeStyles, Typography } from '@material-ui/core';
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
import { AuditLogSettings } from '@app/redesign/features/universe/universe-tabs/db-audit-logs/components/AuditLogSettings';
import { ClusterSpecClusterType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { LogConfigCard } from './LogConfigCard';

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

  const primaryCluster = getClusterByType(universeData!, ClusterSpecClusterType.PRIMARY);
  const universeUuid = universeData?.info?.universe_uuid ?? '';
  const universeName = universeData?.spec?.name ?? '';
  const isAuditLogEnabled = primaryCluster?.audit_log_config?.ysql_audit_config?.enabled;
  const isQueryLogEnabled = primaryCluster?.query_log_config?.ysql_query_log_config?.enabled;

  return (
    <div className={classes.logsTabContainer}>
      <StyledPanel>
        <StyledHeader>{t('troubleshootingLogs')}</StyledHeader>
        <StyledContent>
          <LogConfigCard
            icon={<QueryLogIcon width={20} height={20} />}
            title={t('databaseQueryLog')}
            description={t('databaseQueryLogDescription')}
            learnMoreUrl={QUERY_LOG_DOCS_URL}
            actionLabel={t('enableQueryLogging')}
            actionDisabled={!isUniverseReady || isQueryLogEnabled}
            actionTestId="LogsTab-EnableQueryLoggingButton"
          />
        </StyledContent>
      </StyledPanel>
      <StyledPanel>
        <StyledHeader>{t('complianceLogs')}</StyledHeader>
        <StyledContent>
          <LogConfigCard
            icon={<AuditLogIcon width={20} height={20} />}
            title={t('databaseAuditLog')}
            description={t('databaseAuditLogDescription')}
            learnMoreUrl={AUDIT_LOG_DOCS_URL}
            actionLabel={t('enableAuditLogging')}
            actionDisabled={!isUniverseReady || isAuditLogEnabled}
            actionTestId="LogsTab-EnableAuditLoggingButton"
            onActionClick={() => setAuditLogSettingsModalOpen(true)}
          />
        </StyledContent>
      </StyledPanel>
      <div className={classes.noteBanner}>
        <IdeaIcon width={23} height={24} />
        <Typography className={classes.noteText}>
          <Trans t={t} i18nKey="telemetryExportNote" components={{ bold: <b /> }} />
        </Typography>
      </div>
      {isAuditLogSettingsModalOpen && universeUuid && (
        <AuditLogSettings
          open={isAuditLogSettingsModalOpen}
          onClose={() => setAuditLogSettingsModalOpen(false)}
          auditLogInfo={undefined}
          universeUUID={universeUuid}
          universeName={universeName}
        />
      )}
    </div>
  );
};
