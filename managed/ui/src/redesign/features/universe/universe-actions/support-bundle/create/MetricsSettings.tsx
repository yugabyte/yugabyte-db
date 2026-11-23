import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import {
  mui,
  YBAutoComplete,
  YBButton,
  YBCheckbox,
  YBInput,
  YBSelect
} from '@yugabyte-ui-library/core';

import {
  PromExportType,
  PrometheusMetricsFormat,
  PrometheusMetricsType
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import CardTriangleControlIcon from '@app/redesign/assets/approved/card-triangle-control.svg';

import { EditCustomPrometheusQueriesModal } from '../EditCustomPrometheusQueriesModal';
import { MetricsSettings as MetricsSettingsValue } from '../model/formState';

const { styled, Box, MenuItem, ButtonBase, Typography } = mui;

const TRANSLATION_KEY_PREFIX = 'universeActions.supportBundleV2.create.metrics';

export const SettingsCard = styled(Box)(({ theme }) => ({
  display: 'flex',
  flexDirection: 'column',
  gap: theme.spacing(2.5),
  padding: theme.spacing(2),
  border: `1px solid ${theme.palette.grey[200]}`,
  borderRadius: 6,
  background: theme.palette.grey[50]
}));

const FORMAT_OPTIONS = [PrometheusMetricsFormat.PROM_CHUNK, PrometheusMetricsFormat.PROMQL_JSON];

const toOption = (type: PrometheusMetricsType) => ({ value: type, label: type });

const toNumberOrNull = (value: string) => {
  const parsed = Number(value);
  return value !== '' && parsed > 0 ? parsed : null;
};

interface FormatSelectProps {
  value: PrometheusMetricsFormat;
  onChange: (value: PrometheusMetricsFormat) => void;
  dataTestId: string;
}

export const FormatSelect = ({ value, onChange, dataTestId }: FormatSelectProps) => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  return (
    <YBSelect
      label={t('format')}
      value={value}
      onChange={(event) => onChange(event.target.value as PrometheusMetricsFormat)}
      fullWidth
      dataTestId={dataTestId}
    >
      {FORMAT_OPTIONS.map((format) => (
        <MenuItem key={format} value={format}>
          {t(`formatOption.${format}`)}
        </MenuItem>
      ))}
    </YBSelect>
  );
};

interface PrometheusSettingsProps {
  value: MetricsSettingsValue;
  onChange: (value: MetricsSettingsValue) => void;
  isK8sUniverse: boolean;
}

/** Prometheus collection method, format and the advanced (sources, downsampling, queries) settings. */
export const PrometheusSettings = ({ value, onChange, isK8sUniverse }: PrometheusSettingsProps) => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const [showAdvanced, setShowAdvanced] = useState(false);
  const [isQueriesModalOpen, setIsQueriesModalOpen] = useState(false);
  const isRemoteRead = value.promExportType === PromExportType.REMOTE_READ;
  // PromQL export always downsamples, so the toggle only applies to remote read.
  const isDownSampled = !isRemoteRead || value.promDumpDownSample;
  const metricsTypeOptions = Object.values(PrometheusMetricsType).filter(
    (type) => isK8sUniverse || type !== PrometheusMetricsType.KUBERNETES
  );
  const customQueries = Object.entries(value.promQueries).map(([folderName, query]) => ({
    folderName,
    query
  }));

  return (
    <SettingsCard data-testid="PrometheusSettings">
      <Box display="flex" gap={2}>
        <Box flex={1}>
          <YBSelect
            label={t('collectionMethod')}
            value={value.promExportType}
            onChange={(event) =>
              onChange({ ...value, promExportType: event.target.value as PromExportType })
            }
            fullWidth
            dataTestId="PrometheusSettings-CollectionMethod"
          >
            <MenuItem value={PromExportType.REMOTE_READ}>{t('remoteRead')}</MenuItem>
            <MenuItem value={PromExportType.PROMQL}>{t('promql')}</MenuItem>
          </YBSelect>
        </Box>
        <Box flex={1}>
          {isRemoteRead && (
            <FormatSelect
              value={value.promMetricsFormat}
              onChange={(promMetricsFormat) => onChange({ ...value, promMetricsFormat })}
              dataTestId="PrometheusSettings-Format"
            />
          )}
        </Box>
      </Box>
      <ButtonBase
        onClick={() => setShowAdvanced(!showAdvanced)}
        aria-expanded={showAdvanced}
        data-testid="PrometheusSettings-Advanced"
        sx={{ alignSelf: 'flex-start', gap: 1 }}
      >
        <Box display="flex" sx={{ transform: showAdvanced ? 'rotate(180deg)' : 'rotate(90deg)' }}>
          <CardTriangleControlIcon />
        </Box>
        <Typography variant="body1" fontWeight={500} color="text.primary">
          {t('advanced')}
        </Typography>
      </ButtonBase>
      {showAdvanced && (
        <Box display="flex" flexDirection="column" gap={2.5}>
          <YBAutoComplete
            multiple
            options={metricsTypeOptions.map(toOption)}
            value={value.prometheusMetricsTypes
              .filter((type) => metricsTypeOptions.includes(type))
              .map(toOption)}
            getOptionLabel={(option) => (typeof option === 'string' ? option : option.label)}
            isOptionEqualToValue={(option, selected) => option.value === selected.value}
            onChange={(_event, selected) =>
              onChange({
                ...value,
                prometheusMetricsTypes: (selected as Record<string, string>[]).map(
                  (option) => option.value as PrometheusMetricsType
                )
              })
            }
            ybInputProps={{
              label: t('metricSources'),
              helperText: t('metricSourcesHelper'),
              dataTestId: 'PrometheusSettings-MetricSources-Input'
            }}
            dataTestId="PrometheusSettings-MetricSources"
          />
          {isRemoteRead && (
            <YBCheckbox
              checked={value.promDumpDownSample}
              onChange={(event) => onChange({ ...value, promDumpDownSample: event.target.checked })}
              dataTestId="PrometheusSettings-DownSample"
              label={
                <Box display="flex" flexDirection="column">
                  <Typography variant="body1" fontWeight={600}>
                    {t('downSample')}
                  </Typography>
                  <Typography variant="subtitle1" color="grey.700">
                    {t('downSampleHelper')}
                  </Typography>
                </Box>
              }
            />
          )}
          {isDownSampled && (
            <Box display="flex" gap={1}>
              <YBInput
                type="number"
                label={t('stepSecs')}
                placeholder={t('serverDefault')}
                value={value.stepPromDumpSecs ?? ''}
                onChange={(event) =>
                  onChange({ ...value, stepPromDumpSecs: toNumberOrNull(event.target.value) })
                }
                fullWidth
                dataTestId="PrometheusSettings-Step"
              />
              <YBInput
                type="number"
                label={t('batchMins')}
                tooltip={t('batchMinsTooltip')}
                placeholder={t('serverDefault')}
                value={value.batchDurationPromDumpMins ?? ''}
                onChange={(event) =>
                  onChange({
                    ...value,
                    batchDurationPromDumpMins: toNumberOrNull(event.target.value)
                  })
                }
                fullWidth
                dataTestId="PrometheusSettings-Batch"
              />
            </Box>
          )}
          {!isRemoteRead && (
            <Box display="flex" alignItems="center" gap={2}>
              <Typography variant="body2" color="grey.700">
                {t('customQueryCount', { count: customQueries.length })}
              </Typography>
              <YBButton
                variant="secondary"
                size="small"
                onClick={() => setIsQueriesModalOpen(true)}
                dataTestId="PrometheusSettings-EditQueries"
              >
                {t('editQueries')}
              </YBButton>
            </Box>
          )}
        </Box>
      )}
      {isQueriesModalOpen && (
        <EditCustomPrometheusQueriesModal
          customPrometheusQueries={customQueries}
          updateCustomPrometheusQueries={(queries) =>
            onChange({
              ...value,
              promQueries: Object.fromEntries(
                queries
                  .filter(({ folderName, query }) => folderName && query)
                  .map(({ folderName, query }) => [folderName, query])
              )
            })
          }
          modalProps={{ open: true, onClose: () => setIsQueriesModalOpen(false) }}
        />
      )}
    </SettingsCard>
  );
};
