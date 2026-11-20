import { ReactNode, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { mui, YBCheckbox, YBInput, YBTag, YBTooltip } from '@yugabyte-ui-library/core';

import { SupportBundleComponentType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { formatBytes } from '@app/utils/Formatters';
import CardTriangleControlIcon from '@app/redesign/assets/approved/card-triangle-control.svg';
import InfoIcon from '@app/redesign/assets/approved/info.svg';

import { TimeRangeField } from '../components/TimeRangeField';
import {
  SupportBundleCategoryDef,
  SupportBundleCategoryId,
  SupportBundleComponentDef,
  SupportBundleSelectableComponent,
  SupportBundleUniverseContext
} from '../model/categories';
import { SupportBundleEstimateSummary } from '../model/estimate';
import { METRICS_TIME_RANGE_PRESETS, SupportBundleFormValues } from '../model/formState';
import { FormatSelect, PrometheusSettings, SettingsCard } from './MetricsSettings';

const { styled, Box, Typography, ButtonBase } = mui;

const ONE_GIB_IN_BYTES = 1024 * 1024 * 1024;

const List = styled(Box)(({ theme }) => ({
  flex: 1,
  border: `1px solid ${theme.palette.grey[300]}`,
  borderRadius: 6,
  overflow: 'hidden',
  background: theme.palette.common.white
}));

const CategoryBlock = styled(Box)(({ theme }) => ({
  '&:not(:last-of-type)': {
    borderBottom: `1px solid ${theme.palette.grey[200]}`
  }
}));

const Nested = styled(Box)(({ theme }) => ({
  display: 'flex',
  flexDirection: 'column',
  gap: theme.spacing(1),
  padding: theme.spacing(0, 7, 2, 6.5)
}));

const SizeTag = ({ bytes }: { bytes?: number }) => (
  <YBTag size="small" variant="light" color={bytes !== undefined ? 'primary' : undefined}>
    {bytes !== undefined ? formatBytes(bytes) : '-'}
  </YBTag>
);

interface CustomizeCategoryListProps {
  values: SupportBundleFormValues;
  categories: SupportBundleCategoryDef[];
  context: SupportBundleUniverseContext;
  summary: SupportBundleEstimateSummary | null;
  onChange: (patch: Partial<SupportBundleFormValues>) => void;
}

export const CustomizeCategoryList = ({
  values,
  categories,
  context,
  summary,
  onChange
}: CustomizeCategoryListProps) => {
  const { t } = useTranslation('translation', { keyPrefix: 'universeActions.supportBundleV2' });
  const [expanded, setExpanded] = useState<Set<SupportBundleCategoryId>>(new Set());
  const selected = new Set(values.customComponents);

  const setExpandedFor = (categoryId: SupportBundleCategoryId, isExpanded: boolean) => {
    const next = new Set(expanded);
    if (isExpanded) {
      next.add(categoryId);
    } else {
      next.delete(categoryId);
    }
    setExpanded(next);
  };

  const setComponents = (components: SupportBundleSelectableComponent[], include: boolean) => {
    const next = new Set(selected);
    components.forEach((component) => (include ? next.add(component) : next.delete(component)));
    onChange({ customComponents: Array.from(next) });
  };

  const toggleCategory = (category: SupportBundleCategoryDef, include: boolean) => {
    setComponents(
      category.components.map((def) => def.component),
      include
    );
    // Metrics are the one group that opens on select: most users change their time range.
    if (include && category.id === SupportBundleCategoryId.PERFORMANCE_METRICS) {
      setExpandedFor(category.id, true);
    }
  };

  const renderComponentRow = (def: SupportBundleComponentDef, settings?: ReactNode) => {
    const isSelected = selected.has(def.component);
    return (
      <Box key={def.component} display="flex" flexDirection="column" gap={1}>
        <Box display="flex" alignItems="center" justifyContent="space-between" gap={2}>
          <YBCheckbox
            checked={isSelected}
            onChange={(event) => setComponents([def.component], event.target.checked)}
            dataTestId={`CustomizeComponent-${def.component}`}
            label={
              <Box display="flex" alignItems="center" gap={0.5}>
                <Typography variant="body2" color="text.primary">
                  {t(`component.${def.i18nKey}`)}
                </Typography>
                {def.sensitive && (
                  <YBTooltip title={t(`sensitiveTooltip.${def.i18nKey}`)}>
                    <Box display="flex" color="warning.700">
                      <InfoIcon width={16} height={16} />
                    </Box>
                  </YBTooltip>
                )}
              </Box>
            }
          />
          <SizeTag bytes={isSelected ? summary?.componentBytes[def.component] : undefined} />
        </Box>
        {isSelected && settings}
      </Box>
    );
  };

  const renderCoreFileSettings = () => (
    <SettingsCard>
      <Box display="flex" gap={2}>
        <YBInput
          type="number"
          label={t('create.coreFiles.maxCount')}
          value={values.coreFiles.maxNumRecentCores}
          onChange={(event) =>
            onChange({
              coreFiles: { ...values.coreFiles, maxNumRecentCores: Number(event.target.value) }
            })
          }
          inputProps={{ min: 1 }}
          fullWidth
          dataTestId="CustomizeCoreFiles-MaxCount"
        />
        <YBInput
          type="number"
          label={t('create.coreFiles.maxSizeGb')}
          value={values.coreFiles.maxCoreFileSize / ONE_GIB_IN_BYTES}
          onChange={(event) =>
            onChange({
              coreFiles: {
                ...values.coreFiles,
                maxCoreFileSize: Number(event.target.value) * ONE_GIB_IN_BYTES
              }
            })
          }
          inputProps={{ min: 1 }}
          fullWidth
          dataTestId="CustomizeCoreFiles-MaxSize"
        />
      </Box>
    </SettingsCard>
  );

  const renderMetricsContent = (category: SupportBundleCategoryDef) => (
    <>
      <SettingsCard>
        <TimeRangeField
          label={t('create.timeRange.label')}
          value={values.metrics.timeRange}
          onChange={(timeRange) => onChange({ metrics: { ...values.metrics, timeRange } })}
          presets={METRICS_TIME_RANGE_PRESETS}
          dataTestId="CustomizeMetrics-TimeRange"
        />
      </SettingsCard>
      {category.components.map((def) =>
        renderComponentRow(
          def,
          def.component === SupportBundleComponentType.PrometheusMetrics ? (
            <PrometheusSettings
              value={values.metrics}
              onChange={(metrics) => onChange({ metrics })}
              isK8sUniverse={context.isK8sUniverse}
            />
          ) : (
            <SettingsCard>
              <Box width="50%">
                <FormatSelect
                  value={values.metrics.paMetricsFormat}
                  onChange={(paMetricsFormat) =>
                    onChange({ metrics: { ...values.metrics, paMetricsFormat } })
                  }
                  dataTestId="CustomizeMetrics-PaFormat"
                />
              </Box>
            </SettingsCard>
          )
        )
      )}
    </>
  );

  return (
    <List data-testid="CustomizeCategoryList">
      {categories.map((category) => {
        const selectedCount = category.components.filter((def) =>
          selected.has(def.component)
        ).length;
        const isExpanded = expanded.has(category.id);
        const categoryBytes = selectedCount > 0 ? summary?.categoryBytes[category.id] : undefined;
        const hasSensitive = category.components.some((def) => def.sensitive);
        return (
          <CategoryBlock key={category.id} data-testid={`CustomizeCategory-${category.id}`}>
            <Box display="flex" alignItems="center" justifyContent="space-between" padding={2}>
              <Box display="flex" alignItems="center" gap={1.5}>
                <YBCheckbox
                  checked={selectedCount === category.components.length}
                  indeterminate={selectedCount > 0 && selectedCount < category.components.length}
                  onChange={(event) => toggleCategory(category, event.target.checked)}
                  dataTestId={`CustomizeCategory-${category.id}-Checkbox`}
                  label={
                    <Box>
                      <Typography variant="body1" fontWeight={600} color="text.primary">
                        {t(`category.${category.id}`)}
                      </Typography>
                    </Box>
                  }
                />
                {hasSensitive && (
                  <Box display="flex" alignItems="center" gap={0.5} color="warning.700">
                    <InfoIcon width={16} height={16} />
                    <Typography variant="subtitle1" fontWeight={500} color="warning.700">
                      {t('create.maySensitive')}
                    </Typography>
                  </Box>
                )}
              </Box>
              <Box display="flex" alignItems="center" gap={2}>
                <SizeTag bytes={categoryBytes} />
                <ButtonBase
                  aria-expanded={isExpanded}
                  aria-label={t(`category.${category.id}`)}
                  onClick={() => setExpandedFor(category.id, !isExpanded)}
                  data-testid={`CustomizeCategory-${category.id}-Expand`}
                  sx={{ transform: isExpanded ? undefined : 'rotate(180deg)' }}
                >
                  <CardTriangleControlIcon />
                </ButtonBase>
              </Box>
            </Box>
            {isExpanded && (
              <Nested>
                {category.id === SupportBundleCategoryId.PERFORMANCE_METRICS
                  ? renderMetricsContent(category)
                  : category.components.map((def) =>
                      renderComponentRow(
                        def,
                        def.component === SupportBundleComponentType.CoreFiles
                          ? renderCoreFileSettings()
                          : undefined
                      )
                    )}
              </Nested>
            )}
          </CategoryBlock>
        );
      })}
    </List>
  );
};
