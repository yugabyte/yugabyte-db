import { FC, MouseEvent, ReactNode } from 'react';
import { useTranslation } from 'react-i18next';
import { browserHistory } from 'react-router';
import { KeyboardArrowDown, SettingsOutlined } from '@material-ui/icons';
import { mui, YBButton, YBDropdown, YBSelect } from '@yugabyte-ui-library/core';
import RefreshIcon from '../../../redesign/assets/monitoring/refresh.svg';

const { MenuItem } = mui;

interface IntervalOption {
  label: string;
  selectedLabel: string;
  value: string | number;
}

export interface MonitoringMetricsActionsProps {
  seeQueriesLink?: string | null;
  showCompare: boolean;
  compareDisabled: boolean;
  onCompare: () => void;

  intervalTypes: IntervalOption[];
  refreshInterval: string | number;
  onIntervalChange: (intervalIndex: number) => void;
  refreshDisabled: boolean;
  onRefresh: () => void;

  prometheusQueryEnabled: boolean;
  onTogglePrometheusQuery: () => void;
  onDownloadGrafanaJson: () => void;
  showExportMetrics: boolean;
  exportMetricsIcon?: ReactNode;
  onExportMetrics: () => void;
  showDownloadMetrics: boolean;
  onDownloadMetrics: () => void;
}

export const MonitoringMetricsActions: FC<MonitoringMetricsActionsProps> = ({
  seeQueriesLink,
  showCompare,
  compareDisabled,
  onCompare,
  intervalTypes,
  refreshInterval,
  onIntervalChange,
  refreshDisabled,
  onRefresh,
  prometheusQueryEnabled,
  onTogglePrometheusQuery,
  onDownloadGrafanaJson,
  showExportMetrics,
  exportMetricsIcon,
  onExportMetrics,
  showDownloadMetrics,
  onDownloadMetrics
}) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'clusterDetail.monitoring.metricsHeader'
  });
  const selectedIntervalIndex = Math.max(
    0,
    intervalTypes.findIndex((interval) => interval.value === refreshInterval)
  );

  return (
    <>
      {seeQueriesLink && (
        <YBButton
          variant="ghost"
          size="medium"
          dataTestId="MonitoringMetricsActions-SeeQueries"
          href={seeQueriesLink}
          onClick={(event: MouseEvent) => {
            // A plain click stays in the app; a modified click opens a new tab like a link.
            if (event.button === 0 && !event.metaKey && !event.ctrlKey && !event.shiftKey) {
              event.preventDefault();
              browserHistory.push(seeQueriesLink);
            }
          }}
        >
          {t('seeQueries')}
        </YBButton>
      )}
      {showCompare && (
        <YBButton
          variant="ghost"
          size="medium"
          dataTestId="MonitoringMetricsActions-Compare"
          disabled={compareDisabled}
          onClick={onCompare}
        >
          {t('compare')}
        </YBButton>
      )}
      <YBSelect
        dataTestId="MonitoringMetricsActions-AutoRefresh"
        value={selectedIntervalIndex}
        disabled={refreshDisabled}
        onChange={(event) => onIntervalChange(Number(event.target.value))}
        renderValue={(value) =>
          t('autoRefresh', { interval: intervalTypes[Number(value)]?.selectedLabel })
        }
        sx={{
          width: '200px',
          '& [class*="MuiInputBase-root"]': { height: '32px' },
          '& [class*="MuiInputBase-input"]': { fontSize: '13px', fontWeight: 400 }
        }}
      >
        {intervalTypes.map((interval, index) => (
          <MenuItem key={interval.label} value={index}>
            {interval.label}
          </MenuItem>
        ))}
      </YBSelect>
      <YBButton
        variant="ghost"
        size="medium"
        dataTestId="MonitoringMetricsActions-Refresh"
        startIcon={<RefreshIcon width={24} height={24} />}
        disabled={refreshDisabled}
        onClick={onRefresh}
      >
        {t('refresh')}
      </YBButton>
      <YBDropdown
        dataTestId="MonitoringMetricsActions-Settings"
        origin={
          <YBButton
            variant="ghost"
            size="medium"
            dataTestId="MonitoringMetricsActions-SettingsButton"
            startIcon={<SettingsOutlined fontSize="small" />}
            endIcon={<KeyboardArrowDown />}
          >
            {t('settings')}
          </YBButton>
        }
      >
        <MenuItem
          data-testid="MonitoringMetricsActions-Prometheus"
          onClick={onTogglePrometheusQuery}
        >
          {prometheusQueryEnabled ? t('disablePrometheusQuery') : t('enablePrometheusQuery')}
        </MenuItem>
        <MenuItem data-testid="MonitoringMetricsActions-Grafana" onClick={onDownloadGrafanaJson}>
          {t('downloadGrafanaJson')}
        </MenuItem>
        {showExportMetrics && (
          <MenuItem data-testid="MonitoringMetricsActions-Export" onClick={onExportMetrics}>
            {t('exportMetrics')}
            {exportMetricsIcon}
          </MenuItem>
        )}
        {showDownloadMetrics && (
          <MenuItem data-testid="MonitoringMetricsActions-Download" onClick={onDownloadMetrics}>
            {t('downloadMetrics')}
          </MenuItem>
        )}
      </YBDropdown>
    </>
  );
};
