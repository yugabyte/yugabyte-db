import { useMemo, useState } from 'react';
import { makeStyles, Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { useQuery } from 'react-query';

import { StyledContent, StyledPanel } from '../../../create-universe/components/DefaultComponents';
import { api, telemetryProviderQueryKey } from '@app/redesign/helpers/api';
import { useGetExportTelemetryConfig } from '@app/v2/api/universe/universe';
import {
  isKubernetesUniverse,
  useEditUniverseContext,
  useIsUniverseReady
} from '../../EditUniverseUtils';
import { useExportTelemetryConfigTaskStatus } from '../useExportTelemetryConfigTaskStatus';
import { MetricsExportCard } from './MetricsExportCard';
import { MetricsExportSettingsModal } from './metrics-export/MetricsExportSettingsModal';
import {
  getMetricsExportDisplayInfo,
  isMetricsExportEnabled
} from './metrics-export/metricsExportHelpers';

import MetricsIcon from '@app/redesign/assets/approved/trend-sparkline.svg';

const TRANSLATION_KEY_PREFIX = 'editUniverse.telemetryExport';
const NO_METADATA_FALLBACK = '-';

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
  const { universeData } = useEditUniverseContext();
  const isUniverseReady = useIsUniverseReady();
  const [isMetricsExportModalOpen, setIsMetricsExportModalOpen] = useState(false);

  const universeUuid = universeData?.info?.universe_uuid ?? '';
  const universeName = universeData?.spec?.name ?? '';
  const isKubernetes = universeData ? isKubernetesUniverse(universeData) : false;

  const telemetryConfigQuery = useGetExportTelemetryConfig(universeUuid, undefined, {
    query: { enabled: !!universeUuid }
  });
  const telemetryProvidersQuery = useQuery(telemetryProviderQueryKey.list(), () =>
    api.fetchTelemetryProviderList()
  );

  const isExportEnabled = isMetricsExportEnabled(telemetryConfigQuery.data);
  const { isTelemetryConfigTaskInProgress, isMetricsExportConfiguring } =
    useExportTelemetryConfigTaskStatus(universeUuid);
  const actionDisabled = !isUniverseReady || isTelemetryConfigTaskInProgress || isKubernetes;
  const operation = isExportEnabled ? 'edit' : 'create';

  const metricsExportDisplayInfo = useMemo(
    () => getMetricsExportDisplayInfo(telemetryConfigQuery.data, telemetryProvidersQuery.data),
    [telemetryConfigQuery.data, telemetryProvidersQuery.data]
  );

  return (
    <div className={classes.telemetryExportTabContainer}>
      <StyledPanel>
        <div className={classes.header}>
          <Typography className={classes.title}>{t('title')}</Typography>
          <Typography className={classes.subtitle}>{t('subtitle')}</Typography>
        </div>
        <StyledContent>
          {/* eslint-disable-next-line @typescript-eslint/prefer-nullish-coalescing */}
          {isExportEnabled || isMetricsExportConfiguring ? (
            <MetricsExportCard
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
            />
          ) : (
            <MetricsExportCard
              unconfigured
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
        </StyledContent>
      </StyledPanel>

      {isMetricsExportModalOpen && universeUuid && (
        <MetricsExportSettingsModal
          open={isMetricsExportModalOpen}
          operation={operation}
          universeUuid={universeUuid}
          universeName={universeName}
          onClose={() => setIsMetricsExportModalOpen(false)}
        />
      )}
    </div>
  );
};
