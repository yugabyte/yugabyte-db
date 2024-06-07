import { Box, makeStyles, useTheme } from '@material-ui/core';
import { ToggleButton, ToggleButtonGroup } from '@material-ui/lab';
import { useTranslation } from 'react-i18next';

import { YBInput } from '../../../../redesign/components';
import { NodeAggregation, SplitMode, SplitType } from '../../../metrics/dtos';

interface MetricsFilterProps {
  metricsNodeAggregation: NodeAggregation;
  metricsSplitType: SplitType;
  metricsSplitMode: SplitMode;
  metricsSplitCount: number;
  metricsSplitTypeOptions: SplitType[]; // Not all SplitType options apply for every metrics.
  setMetricsNodeAggregation: (metricsNodeAggregation: NodeAggregation) => void;
  setMetricsSplitType: (metricsSplitType: SplitType) => void;
  setMetricsSplitMode: (metricsSplitMode: SplitMode) => void;
  setMetricsSplitCount: (metricsSplitCount: number) => void;
}

const useStyles = makeStyles(() => ({
  numericInput: {
    width: '60px'
  }
}));

const TRANSLATION_KEY_PREFIX = 'clusterDetail.xCluster.metricsPanel.graphFilter';

export const MetricsFilter = ({
  metricsNodeAggregation,
  metricsSplitType,
  metricsSplitMode,
  metricsSplitCount,
  metricsSplitTypeOptions,
  setMetricsNodeAggregation,
  setMetricsSplitType,
  setMetricsSplitMode,
  setMetricsSplitCount
}: MetricsFilterProps) => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const theme = useTheme();
  const classes = useStyles();

  const handleMetricNodeAggregationChange = (
    _event: React.MouseEvent<HTMLElement>,
    metricsNodeAggregation: NodeAggregation | null
  ) => {
    if (metricsNodeAggregation !== null) {
      setMetricsNodeAggregation(metricsNodeAggregation);
    }
  };
  const handleMetricsSplitTypeChange = (
    _event: React.MouseEvent<HTMLElement>,
    metricsSplitType: SplitType | null
  ) => {
    switch (metricsSplitType) {
      case null:
        return;
      case SplitType.NONE:
        setMetricsSplitMode(SplitMode.NONE);
        setMetricsSplitType(metricsSplitType);
        return;
      case SplitType.TABLE:
        // SplitMode.NONE is not enabled for table splits to avoid showing thousands of
        // metric lines on the graph at once.
        if (metricsSplitMode === SplitMode.NONE) {
          setMetricsSplitMode(SplitMode.TOP);
        }
        setMetricsSplitType(metricsSplitType);
        return;
      default:
        setMetricsSplitType(metricsSplitType);
    }
  };
  const handleMetricsSplitModeChange = (
    _event: React.MouseEvent<HTMLElement>,
    metricsSplitMode: SplitMode | null
  ) => {
    if (metricsSplitMode !== null) {
      setMetricsSplitMode(metricsSplitMode);
    }
  };
  const handleMetricsSplitCountChange = (event: React.ChangeEvent<HTMLInputElement>) => {
    setMetricsSplitCount(parseInt(event.target.value));
  };

  return (
    <Box display="flex" gridGap={theme.spacing(1)}>
      <ToggleButtonGroup
        value={metricsNodeAggregation}
        exclusive
        onChange={handleMetricNodeAggregationChange}
      >
        <ToggleButton value={NodeAggregation.MAX}>{t('nodeAggregation.max')}</ToggleButton>
        <ToggleButton value={NodeAggregation.MIN}>{t('nodeAggregation.min')}</ToggleButton>
        <ToggleButton value={NodeAggregation.AVERAGE}>{t('nodeAggregation.average')}</ToggleButton>
      </ToggleButtonGroup>
      <ToggleButtonGroup value={metricsSplitType} exclusive onChange={handleMetricsSplitTypeChange}>
        {/* Some split type options don't make sense / are unavailable for some certain metrics. 
            To avoid making metrics queries with filters that don't apply, we'll just hide 
            the subset of options from the user. */}
        {metricsSplitTypeOptions.includes(SplitType.NONE) && (
          <ToggleButton value={SplitType.NONE}>{t('splitType.cluster')}</ToggleButton>
        )}
        {metricsSplitTypeOptions.includes(SplitType.NODE) && (
          <ToggleButton value={SplitType.NODE}>{t('splitType.node')}</ToggleButton>
        )}
        {metricsSplitTypeOptions.includes(SplitType.NAMESPACE) && (
          <ToggleButton value={SplitType.NAMESPACE}>{t('splitType.namespace')}</ToggleButton>
        )}
        {metricsSplitTypeOptions.includes(SplitType.TABLE) && (
          <ToggleButton value={SplitType.TABLE}>{t('splitType.table')}</ToggleButton>
        )}
      </ToggleButtonGroup>
      {metricsSplitType !== SplitType.NONE && (
        <Box display="flex" gridGap={theme.spacing(1)}>
          <ToggleButtonGroup
            value={metricsSplitMode}
            exclusive
            onChange={handleMetricsSplitModeChange}
          >
            {metricsSplitType !== SplitType.TABLE && (
              <ToggleButton value={SplitMode.NONE}>{t('splitMode.all')}</ToggleButton>
            )}
            <ToggleButton value={SplitMode.TOP}>{t('splitMode.top')}</ToggleButton>
            <ToggleButton value={SplitMode.BOTTOM}>{t('splitMode.bottom')}</ToggleButton>
          </ToggleButtonGroup>
          {metricsSplitMode !== SplitMode.NONE && (
            <YBInput
              className={classes.numericInput}
              type="number"
              inputProps={{ min: 1, 'data-testid': 'XClusterMetrics-SplitCountInput' }}
              value={metricsSplitCount}
              onChange={handleMetricsSplitCountChange}
              inputMode="numeric"
            />
          )}
        </Box>
      )}
    </Box>
  );
};
