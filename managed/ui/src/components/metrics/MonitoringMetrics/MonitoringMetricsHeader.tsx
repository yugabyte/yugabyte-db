import { FC, ReactNode, useEffect, useState } from 'react';
import { useTranslation } from 'react-i18next';
import moment from 'moment-timezone';
import {
  mui,
  YBButton,
  YBButtonGroup,
  YBButtonProps,
  YBDatePicker,
  YBInput,
  YBSelect
} from '@yugabyte-ui-library/core';
import {
  MetricConsts,
  MetricMeasure,
  NodeType,
  MIN_OUTLIER_NUM_NODES,
  MAX_OUTLIER_NUM_NODES,
  MAX_OUTLIER_NUM_TABLES
} from '../constants';
import { SplitType } from '../dtos';
import { getUniverseRegionOptions } from '../MetricsComparisonModal/RegionSelector';
import { getUniverseNodeOptions } from '../MetricsComparisonModal/NodeSelector';
import { ALL_NODE_TYPES, getNodeTypeAvailability } from '../NodeTypeSelector/NodeTypeSelector';
import TreeIcon from '../../../redesign/assets/monitoring/tree-icon.svg?img';

const { Box, Divider, MenuItem, Typography } = mui;

const ZONE_PREFIX = 'zone:';
const NODE_PREFIX = 'node:';

// Matches the selects of Advanced Metrics: 32px tall with 13px text.
const SELECT_SX = {
  '& [class*="MuiInputBase-root"]': { height: '32px' },
  '& [class*="MuiInputBase-input"]': { fontSize: '13px', fontWeight: 400 }
};

// The timezone picker is YBA's MUI v4 autocomplete; bring it to the same 32px height.
const TIMEZONE_SX = {
  '& [class*="MuiInputBase-root"]': { height: '32px', minHeight: '32px', py: 0 },
  '& [class*="MuiInputBase-input"]': { fontSize: '13px' }
};

// The date pickers show wall-clock time in the metrics timezone, which can differ from the
// browser's; convert to and from it.
const PICKER_FORMAT = 'YYYY-MM-DDTHH:mm:ss';
const isKnownZone = (timezone?: string): timezone is string =>
  !!timezone && !!moment.tz.zone(timezone);
const toPickerDate = (date: Date, timezone?: string) =>
  isKnownZone(timezone) ? new Date(moment(date).tz(timezone).format(PICKER_FORMAT)) : date;
const fromPickerDate = (date: Date, timezone?: string) =>
  isKnownZone(timezone) ? moment.tz(moment(date).format(PICKER_FORMAT), timezone).toDate() : date;

const DATE_PICKER_SX = {
  '& [class*="MuiInputBase-root"]': { height: '32px' },
  '& [class*="MuiInputBase-input"]': { fontSize: '13px' }
};

const TreeMarker = () => <img src={TreeIcon} alt="" style={{ marginRight: 8 }} />;

export interface FilterTypeOption {
  label?: string;
  type: string;
  value?: string;
}

interface MetricMeasureOption {
  value: string;
  label: string;
  k8label?: string;
}

export interface CustomTimeRange {
  start: Date;
  end: Date;
  timezone?: string;
  onChange: (start: Date, end: Date) => void;
}

export interface MonitoringMetricsHeaderProps {
  currentTimestamp: ReactNode;
  timezoneSelector?: ReactNode;
  customRange?: CustomTimeRange | null;
  filterTypes: FilterTypeOption[];
  filterLabel: string;
  onFilterChange: (filterIndex: number) => void;

  universe: any;
  provider?: any;
  currentSelectedRegion: string;
  selectedRegionClusterUUID: string | null;
  selectedRegionCode: string | null;
  selectedZoneName: string | null;
  selectedNode: string;
  currentSelectedNodeType: string;
  isDedicatedNodes: boolean;
  onRegionChanged: (displayName: string, regionCode?: string | null, clusterUUID?: string) => void;
  onNodeTypeChanged: (nodeType: string) => void;
  onNodeChanged: (nodeName: string, zoneName: string | null) => void;

  showSplitSelector: boolean;
  metricMeasureTypes: MetricMeasureOption[];
  metricMeasure: string;
  onMetricMeasureChanged: (metricMeasure: string) => void;
  isSingleNodeSelected: boolean;
  isK8Universe: boolean;
  outlierTypes: { value: string; label: string }[];
  outlierType: string;
  onOutlierTypeChanged: (outlierType: string) => void;
  outlierNumNodes: number;
  onOutlierNumNodesChanged: (numNodes: number) => void;
  splitType: string;
}

export const MonitoringMetricsHeader: FC<MonitoringMetricsHeaderProps> = ({
  currentTimestamp,
  timezoneSelector,
  customRange,
  filterTypes,
  filterLabel,
  onFilterChange,
  universe,
  provider,
  currentSelectedRegion,
  selectedRegionClusterUUID,
  selectedRegionCode,
  selectedZoneName,
  selectedNode,
  currentSelectedNodeType,
  isDedicatedNodes,
  onRegionChanged,
  onNodeTypeChanged,
  onNodeChanged,
  showSplitSelector,
  metricMeasureTypes,
  metricMeasure,
  onMetricMeasureChanged,
  isSingleNodeSelected,
  isK8Universe,
  outlierTypes,
  outlierType,
  onOutlierTypeChanged,
  outlierNumNodes,
  onOutlierNumNodesChanged,
  splitType
}) => {
  const { t } = useTranslation('translation', { keyPrefix: 'clusterDetail.monitoring' });

  const regionOptions = getUniverseRegionOptions(universe, provider);
  const selectedRegionOption = regionOptions.options.find(
    (option) =>
      option.label === currentSelectedRegion && option.clusterUUID === selectedRegionClusterUUID
  );

  const nodeOptions = getUniverseNodeOptions({
    selectedUniverse: universe,
    selectedRegionClusterUUID,
    selectedRegionCode,
    currentSelectedNodeType
  }).options;
  const nodeValue = selectedZoneName
    ? `${ZONE_PREFIX}${selectedZoneName}`
    : selectedNode && selectedNode !== MetricConsts.ALL
      ? `${NODE_PREFIX}${selectedNode}`
      : MetricConsts.ALL;
  // A node type switch can filter the selected zone or node out of the list.
  const selectedNodeValue = nodeOptions.some(
    (option) =>
      (option.kind === 'zone'
        ? `${ZONE_PREFIX}${option.zone}`
        : `${NODE_PREFIX}${option.nodeName}`) === nodeValue
  )
    ? nodeValue
    : MetricConsts.ALL;

  const nodeTypeAvailability = isDedicatedNodes
    ? getNodeTypeAvailability({
        currentSelectedNodeType,
        selectedUniverse: universe,
        selectedRegionClusterUUID,
        selectedRegionCode
      })
    : null;

  const selectedFilterIndex = filterTypes.findIndex((filter) => filter.label === filterLabel);
  const isOutlierView = metricMeasure !== MetricMeasure.OVERALL;
  const maxOutlierValue =
    splitType === SplitType.NODE ? MAX_OUTLIER_NUM_NODES : MAX_OUTLIER_NUM_TABLES;
  const outlierUnit =
    splitType === SplitType.NODE
      ? isK8Universe
        ? t('metricsHeader.unitPods')
        : t('metricsHeader.unitNodes')
      : splitType === SplitType.TABLE
        ? t('metricsHeader.unitTables')
        : t('metricsHeader.unitDatabases');

  // Picker edits stay local until Apply, as with the other layout's date picker.
  const [pendingRange, setPendingRange] = useState<{ start: Date; end: Date } | null>(null);
  useEffect(() => {
    setPendingRange(null);
  }, [customRange?.start?.getTime(), customRange?.end?.getTime()]);
  const shownRange =
    pendingRange ?? (customRange ? { start: customRange.start, end: customRange.end } : null);
  const isRangeInvalid = !!shownRange && shownRange.start.getTime() >= shownRange.end.getTime();
  const applyRange = () => {
    if (customRange && pendingRange && !isRangeInvalid) {
      customRange.onChange(pendingRange.start, pendingRange.end);
    }
  };

  // Typing goes through local text so the field can be cleared mid-edit; only a number in range is
  // applied, and leaving the field puts the applied value back.
  const [outlierNumText, setOutlierNumText] = useState(String(outlierNumNodes));
  useEffect(() => {
    setOutlierNumText(String(outlierNumNodes));
  }, [outlierNumNodes]);
  const handleOutlierNumChange = (text: string) => {
    setOutlierNumText(text);
    const numNodes = Number(text);
    if (
      text !== '' &&
      Number.isInteger(numNodes) &&
      numNodes >= MIN_OUTLIER_NUM_NODES &&
      numNodes <= maxOutlierValue &&
      numNodes !== outlierNumNodes
    ) {
      onOutlierNumNodesChanged(numNodes);
    }
  };

  return (
    <Box data-testid="MonitoringMetricsHeader" sx={{ mt: 2.5 }}>
      <Box sx={{ display: 'flex', alignItems: 'center', gap: 1, mb: 2.5, minHeight: '32px' }}>
        <Typography variant="h5" sx={{ fontSize: '15px', fontWeight: 600, lineHeight: '20px' }}>
          {t('classicMetrics')}
        </Typography>
        <Box
          sx={{
            display: 'flex',
            alignItems: 'center',
            flexWrap: 'wrap',
            justifyContent: 'flex-end',
            gap: 1,
            ml: 'auto'
          }}
        >
          <Typography
            variant="body2"
            component="span"
            sx={(theme) => ({ color: theme.palette.grey[600], mr: 1 })}
          >
            {t('metricsHeader.currentTimestamp')} {currentTimestamp}
          </Typography>
          {timezoneSelector && <Box sx={TIMEZONE_SX}>{timezoneSelector}</Box>}
          {customRange && shownRange && (
            <>
              <Box sx={DATE_PICKER_SX}>
                <YBDatePicker
                  dataTestid="MonitoringMetricsHeader-CustomStart"
                  value={toPickerDate(shownRange.start, customRange.timezone)}
                  validation={{ maxDate: toPickerDate(new Date(), customRange.timezone) }}
                  onChange={(value) =>
                    value &&
                    setPendingRange({
                      start: fromPickerDate(value, customRange.timezone),
                      end: shownRange.end
                    })
                  }
                  error={isRangeInvalid}
                  helperText={isRangeInvalid ? t('metricsHeader.invalidRange') : undefined}
                  calendar={{ showTimeSelector: true }}
                />
              </Box>
              <Box sx={DATE_PICKER_SX}>
                <YBDatePicker
                  dataTestid="MonitoringMetricsHeader-CustomEnd"
                  value={toPickerDate(shownRange.end, customRange.timezone)}
                  validation={{
                    minDate: toPickerDate(shownRange.start, customRange.timezone),
                    maxDate: toPickerDate(new Date(), customRange.timezone)
                  }}
                  onChange={(value) =>
                    value &&
                    setPendingRange({
                      start: shownRange.start,
                      end: fromPickerDate(value, customRange.timezone)
                    })
                  }
                  error={isRangeInvalid}
                  calendar={{ showTimeSelector: true }}
                />
              </Box>
              <YBButton
                variant="secondary"
                size="medium"
                dataTestId="MonitoringMetricsHeader-ApplyCustomRange"
                disabled={!pendingRange || isRangeInvalid}
                onClick={applyRange}
              >
                {t('metricsHeader.apply')}
              </YBButton>
            </>
          )}
          <YBSelect
            dataTestId="MonitoringMetricsHeader-TimeRange"
            value={selectedFilterIndex >= 0 ? selectedFilterIndex : 0}
            onChange={(event) => onFilterChange(Number(event.target.value))}
            sx={{ width: '180px', ...SELECT_SX }}
          >
            {filterTypes.map((filter, index) =>
              filter.type === 'divider' ? (
                <Divider key={`divider-${index}`} />
              ) : (
                <MenuItem key={filter.label} value={index}>
                  {filter.label}
                </MenuItem>
              )
            )}
          </YBSelect>
        </Box>
      </Box>

      <Box sx={{ display: 'flex', alignItems: 'center' }}>
        <YBSelect
          dataTestId="MonitoringMetricsHeader-Region"
          value={selectedRegionOption?.key ?? MetricConsts.ALL}
          disabled={regionOptions.isDisabled}
          tooltip={regionOptions.isDisabled ? t('metricsHeader.singleRegion') : undefined}
          onChange={(event) => {
            const option = regionOptions.options.find((item) => item.key === event.target.value);
            option
              ? onRegionChanged(option.label, option.regionCode, option.clusterUUID)
              : onRegionChanged(MetricConsts.ALL);
          }}
          menuProps={{ PaperProps: { style: { maxHeight: '400px' } } }}
          sx={{
            width: '300px',
            ...SELECT_SX,
            '& [class*="MuiInputBase-root"]': {
              height: '32px',
              borderTopRightRadius: 0,
              borderBottomRightRadius: 0
            }
          }}
        >
          <MenuItem value={MetricConsts.ALL}>{t('metricsHeader.allRegions')}</MenuItem>
          {regionOptions.options.length > 0 && <Divider />}
          {regionOptions.options.flatMap((option) => [
            ...(option.startsGroup ? [<Divider key={`${option.key}-divider`} />] : []),
            <MenuItem
              key={option.key}
              value={option.key}
              sx={(theme) => (option.isCluster ? {} : { color: theme.palette.grey[600] })}
            >
              {!option.isCluster && regionOptions.options.some((item) => item.isCluster) && (
                <TreeMarker />
              )}
              {option.label}
            </MenuItem>
          ])}
        </YBSelect>

        {nodeTypeAvailability && (
          <YBSelect
            dataTestId="MonitoringMetricsHeader-NodeType"
            value={currentSelectedNodeType}
            onChange={(event) => onNodeTypeChanged(event.target.value)}
            sx={{
              width: '180px',
              ...SELECT_SX,
              '& [class*="MuiInputBase-root"]': { height: '32px', borderRadius: 0, borderLeft: 0 }
            }}
          >
            <MenuItem value={NodeType.ALL}>{ALL_NODE_TYPES}</MenuItem>
            {nodeTypeAvailability.isMaster && (
              <MenuItem value={NodeType.MASTER}>{NodeType.MASTER}</MenuItem>
            )}
            {nodeTypeAvailability.isTServer && (
              <MenuItem value={NodeType.TSERVER}>{NodeType.TSERVER}</MenuItem>
            )}
          </YBSelect>
        )}

        <YBSelect
          dataTestId="MonitoringMetricsHeader-ZoneNode"
          value={selectedNodeValue}
          disabled={universe === MetricConsts.ALL}
          onChange={(event) => {
            const value = String(event.target.value);
            if (value.startsWith(ZONE_PREFIX)) {
              onNodeChanged(MetricConsts.ALL, value.slice(ZONE_PREFIX.length));
            } else if (value.startsWith(NODE_PREFIX)) {
              onNodeChanged(value.slice(NODE_PREFIX.length), null);
            } else {
              onNodeChanged(MetricConsts.ALL, null);
            }
          }}
          menuProps={{ PaperProps: { style: { maxHeight: '400px' } } }}
          sx={{
            width: '300px',
            ...SELECT_SX,
            '& [class*="MuiInputBase-root"]': {
              height: '32px',
              borderTopLeftRadius: 0,
              borderBottomLeftRadius: 0
            }
          }}
        >
          <MenuItem value={MetricConsts.ALL}>{t('metricsHeader.allZones')}</MenuItem>
          {nodeOptions.flatMap((option) =>
            option.kind === 'zone'
              ? [
                  <Divider key={`${option.key}-divider`} />,
                  <MenuItem key={option.key} value={`${ZONE_PREFIX}${option.zone}`}>
                    {option.zone}
                  </MenuItem>
                ]
              : [
                  <MenuItem
                    key={option.key}
                    value={`${NODE_PREFIX}${option.nodeName}`}
                    sx={(theme) => ({ color: theme.palette.grey[600] })}
                  >
                    <TreeMarker />
                    {option.nodeName}
                    {isDedicatedNodes && (
                      <Box component="span" sx={{ ml: 1 }}>
                        {option.nodeType}
                      </Box>
                    )}
                    <Box component="span" sx={{ ml: 1 }}>
                      {option.privateIp}
                    </Box>
                  </MenuItem>
                ]
          )}
        </YBSelect>
      </Box>

      {showSplitSelector && (
        <Box sx={{ display: 'flex', alignItems: 'center', gap: 2, mt: 2 }}>
          <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
            <img src={TreeIcon} alt="" />
            <YBButtonGroup
              divider
              dataTestId="MonitoringMetricsHeader-MetricMeasure"
              value={metricMeasure}
              buttons={metricMeasureTypes.map((measure) => {
                const isDisabled = measure.value === MetricMeasure.OUTLIER && isSingleNodeSelected;
                return {
                  value: measure.value,
                  label:
                    isK8Universe && measure.value === MetricMeasure.OUTLIER && measure.k8label
                      ? measure.k8label
                      : measure.label,
                  onClick: () => onMetricMeasureChanged(measure.value),
                  tooltip: isDisabled ? t('metricsHeader.outlierDisabled') : undefined,
                  // A `dataTestId` here would reach the DOM button.
                  buttonProps: { disabled: isDisabled } as YBButtonProps
                };
              })}
            />
          </Box>

          {isOutlierView && (
            <Box sx={{ display: 'flex', alignItems: 'center', gap: 2 }}>
              <Box
                sx={(theme) => ({ width: '1px', height: '24px', bgcolor: theme.palette.grey[300] })}
              />
              <YBButtonGroup
                divider
                dataTestId="MonitoringMetricsHeader-OutlierType"
                value={outlierType}
                buttons={outlierTypes.map((outlier) => ({
                  value: outlier.value,
                  label: outlier.label,
                  onClick: () => onOutlierTypeChanged(outlier.value)
                }))}
              />
              <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
                <YBInput
                  type="number"
                  dataTestId="MonitoringMetricsHeader-OutlierCount"
                  value={outlierNumText}
                  inputProps={{ min: MIN_OUTLIER_NUM_NODES, max: maxOutlierValue }}
                  onChange={(event) => handleOutlierNumChange(event.target.value)}
                  onBlur={() => setOutlierNumText(String(outlierNumNodes))}
                  sx={{ width: '72px', ...SELECT_SX }}
                />
                <Typography variant="body2">{outlierUnit}</Typography>
              </Box>
            </Box>
          )}
        </Box>
      )}
    </Box>
  );
};
