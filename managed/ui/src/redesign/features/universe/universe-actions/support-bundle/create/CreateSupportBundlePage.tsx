import { useMemo, useRef, useState } from 'react';
import { AxiosError } from 'axios';
import { Controller, useForm, useWatch } from 'react-hook-form';
import { toast } from 'react-toastify';
import { useTranslation } from 'react-i18next';
import { mui } from '@yugabyte-ui-library/core';

import { Universe } from '@app/redesign/features/universe/universe-form/utils/dto';
import { handleServerError } from '@app/utils/errorHandlingUtils';
import CloseLargeIcon from '@app/redesign/assets/approved/close-large.svg';
import LogoYugabyteIcon from '@app/redesign/assets/approved/logo-yugabyte.svg';
import TreeIcon from '@app/redesign/assets/approved/tree-icon.svg';

import { CategoryRow } from '../components/CategoryRow';
import { NodeScopeSelect } from '../components/NodeScopeSelect';
import { RadioCard } from '../components/RadioCard';
import { TimeRangeField } from '../components/TimeRangeField';
import { useCreateSupportBundleV2, useSupportBundleEstimate } from '../hooks/useSupportBundles';
import {
  getAvailableCategories,
  getRecommendedComponents,
  SupportBundleCategoryId,
  SupportBundleUniverseContext
} from '../model/categories';
import { summarizeEstimate } from '../model/estimate';
import {
  BUNDLE_TIME_RANGE_PRESETS,
  CollectionMode,
  getDefaultFormValues,
  METRICS_TIME_RANGE_PRESETS,
  SupportBundleFormValues
} from '../model/formState';
import { groupNodesByRegion } from '../model/nodes';
import { getSelectedComponents, toCreateSpec } from '../model/toCreateSpec';
import { SummaryPanel } from './SummaryPanel';

const { styled, Box, Dialog, Typography, IconButton } = mui;

const TRANSLATION_KEY_PREFIX = 'universeActions.supportBundleV2.create';

const Header = styled(Box)(({ theme }) => ({
  display: 'flex',
  alignItems: 'center',
  gap: theme.spacing(2),
  padding: theme.spacing(2, 3, 2, 2.5),
  background: theme.palette.common.white,
  borderBottom: `1px solid ${theme.palette.grey[200]}`,
  boxShadow: '0px 2px 6px rgba(102, 102, 102, 0.15)'
}));

const CloseButton = styled(IconButton)(({ theme }) => ({
  padding: theme.spacing(0.5),
  background: theme.palette.grey[300],
  color: theme.palette.grey[900],
  '&:hover': {
    background: theme.palette.grey[400]
  }
}));

const Section = styled(Box)(({ theme }) => ({
  display: 'flex',
  flexDirection: 'column',
  gap: theme.spacing(2.5),
  padding: theme.spacing(3),
  border: `1px solid ${theme.palette.grey[300]}`,
  borderRadius: 8,
  background: theme.palette.common.white
}));

const GroupDivider = styled(Box)(({ theme }) => ({
  display: 'flex',
  alignItems: 'center',
  gap: theme.spacing(1),
  color: theme.palette.grey[500],
  '&::after': {
    content: '""',
    flex: 1,
    borderTop: `1px solid ${theme.palette.grey[300]}`
  }
}));

const CategoryList = styled(Box)(({ theme }) => ({
  marginLeft: theme.spacing(4),
  border: `1px solid ${theme.palette.grey[300]}`,
  borderRadius: 8,
  overflow: 'hidden'
}));

const NestedSettings = styled(Box)(({ theme }) => ({
  marginLeft: theme.spacing(4),
  padding: theme.spacing(2),
  border: `1px solid ${theme.palette.grey[200]}`,
  borderRadius: 8,
  background: theme.palette.grey[50]
}));

interface CreateSupportBundlePageProps {
  universe: Universe;
  context: SupportBundleUniverseContext;
  onClose: () => void;
  onCreated: () => void;
}

export const CreateSupportBundlePage = ({
  universe,
  context,
  onClose,
  onCreated
}: CreateSupportBundlePageProps) => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const { t: tCommon } = useTranslation('translation', { keyPrefix: 'common' });
  const universeUuid = universe.universeUUID;
  const regions = useMemo(
    () => groupNodesByRegion(universe.universeDetails.nodeDetailsSet),
    [universe]
  );
  const nodeCount = regions.reduce((count, region) => count + region.nodeNames.length, 0);
  const categories = getAvailableCategories(context);
  const recommendedCategories = categories.filter((category) => category.recommended);
  const metricsCategory = categories.find(
    (category) => category.id === SupportBundleCategoryId.PERFORMANCE_METRICS
  );

  const { control, setValue } = useForm<SupportBundleFormValues>({
    defaultValues: getDefaultFormValues(getRecommendedComponents(context))
  });
  const values = useWatch({ control }) as SupportBundleFormValues;
  const [expandedCard, setExpandedCard] = useState<CollectionMode | null>(values.mode);
  const [isNodesSelectOpen, setIsNodesSelectOpen] = useState(false);
  const nodesFieldRef = useRef<HTMLDivElement>(null);

  // The estimate's time window is anchored here and only moves on "Refresh estimate", so the
  // spec (and the estimate query key) stays put while the user edits the form.
  const [estimateNow, setEstimateNow] = useState(() => new Date());
  const estimateSpec = toCreateSpec(values, context, estimateNow);
  const estimate = useSupportBundleEstimate(universeUuid, estimateSpec);
  const summary = estimate.data ? summarizeEstimate(estimate.data) : null;
  const createBundle = useCreateSupportBundleV2(universeUuid);

  const selectedComponents = getSelectedComponents(values, context);
  const selectedCategories = categories.filter((category) =>
    category.components.some((def) => selectedComponents.includes(def.component))
  );

  const selectMode = (mode: CollectionMode) => {
    setValue('mode', mode);
    setExpandedCard(mode);
  };
  const toggleExpanded = (mode: CollectionMode) =>
    setExpandedCard(expandedCard === mode ? null : mode);

  const scopeToFewerNodes = () => {
    nodesFieldRef.current?.scrollIntoView({ behavior: 'smooth', block: 'center' });
    setIsNodesSelectOpen(true);
  };

  const onCreate = () => {
    const spec = toCreateSpec(values, context, new Date());
    if (!spec) {
      return;
    }
    createBundle.mutate(spec, {
      onSuccess: () => {
        toast.success(t('createSuccess'));
        onCreated();
      },
      onError: (error) =>
        handleServerError(error as AxiosError, { customErrorLabel: t('createFailed') })
    });
  };

  return (
    <Dialog open fullScreen onClose={onClose} data-testid="CreateSupportBundlePage">
      <Header>
        <LogoYugabyteIcon />
        <Box flex={1} display="flex" flexDirection="column" gap={0.5}>
          <Typography
            variant="h4"
            sx={(theme) => ({
              color: theme.palette.marketing?.indigo ?? theme.palette.text.primary
            })}
          >
            {t('title')}
          </Typography>
          <Typography variant="body2" color="grey.700">
            {t('subtitle', {
              universeName: universe.name,
              regions: t('regionCount', { count: regions.length }),
              nodes: t('nodeCount', { count: nodeCount })
            })}
          </Typography>
        </Box>
        <CloseButton
          aria-label={tCommon('close')}
          onClick={onClose}
          data-testid="CreateSupportBundlePage-Close"
        >
          <CloseLargeIcon />
        </CloseButton>
      </Header>
      <Box display="flex" alignItems="flex-start" gap={3} padding={5} bgcolor="grey.50" flex={1}>
        <Box display="flex" flexDirection="column" gap={3} width={805}>
          <Section>
            <Typography variant="h5">{t('timeAndScope')}</Typography>
            <Controller
              control={control}
              name="timeRange"
              render={({ field }) => (
                <TimeRangeField
                  label={t('timeRange.label')}
                  value={field.value}
                  onChange={field.onChange}
                  presets={BUNDLE_TIME_RANGE_PRESETS}
                  dataTestId="CreateSupportBundlePage-TimeRange"
                />
              )}
            />
            <Box width={275} ref={nodesFieldRef}>
              <Controller
                control={control}
                name="nodeScope"
                render={({ field }) => (
                  <NodeScopeSelect
                    value={field.value}
                    onChange={field.onChange}
                    regions={regions}
                    open={isNodesSelectOpen}
                    onOpenChange={setIsNodesSelectOpen}
                    dataTestId="CreateSupportBundlePage-Nodes"
                  />
                )}
              />
            </Box>
          </Section>
          <Section>
            <Typography variant="h5">{t('whatToCollect')}</Typography>
            <RadioCard
              title={t('recommended.title')}
              description={t('recommended.description')}
              selected={values.mode === CollectionMode.RECOMMENDED}
              expanded={expandedCard === CollectionMode.RECOMMENDED}
              onSelect={() => selectMode(CollectionMode.RECOMMENDED)}
              onToggleExpanded={() => toggleExpanded(CollectionMode.RECOMMENDED)}
              dataTestId="CreateSupportBundlePage-Recommended"
            >
              <Box display="flex" flexDirection="column" gap={2}>
                <Box display="flex" flexDirection="column" gap={0.5}>
                  <GroupDivider>
                    <TreeIcon />
                    <Typography variant="subtitle1" color="grey.700">
                      {t('recommended.includedByDefault')}
                    </Typography>
                  </GroupDivider>
                  <CategoryList>
                    {recommendedCategories.map((category) => (
                      <CategoryRow
                        key={category.id}
                        category={category}
                        checked={!values.recommendedExcludedCategories.includes(category.id)}
                        onToggle={(checked) =>
                          setValue(
                            'recommendedExcludedCategories',
                            checked
                              ? values.recommendedExcludedCategories.filter(
                                  (id) => id !== category.id
                                )
                              : [...values.recommendedExcludedCategories, category.id]
                          )
                        }
                        sizeBytes={summary?.categoryBytes[category.id]}
                        dataTestId={`CreateSupportBundlePage-Category-${category.id}`}
                      />
                    ))}
                  </CategoryList>
                </Box>
                {metricsCategory && (
                  <Box display="flex" flexDirection="column" gap={0.5}>
                    <GroupDivider>
                      <TreeIcon />
                      <Typography variant="subtitle1" color="grey.700">
                        {t('recommended.optional')}
                      </Typography>
                    </GroupDivider>
                    <CategoryList>
                      <CategoryRow
                        category={metricsCategory}
                        checked={values.recommendedIncludeMetrics}
                        onToggle={(checked) => setValue('recommendedIncludeMetrics', checked)}
                        sizeBytes={summary?.categoryBytes[metricsCategory.id]}
                        dataTestId={`CreateSupportBundlePage-Category-${metricsCategory.id}`}
                      >
                        {values.recommendedIncludeMetrics && (
                          <NestedSettings>
                            <Controller
                              control={control}
                              name="metrics.timeRange"
                              render={({ field }) => (
                                <TimeRangeField
                                  label={t('timeRange.label')}
                                  value={field.value}
                                  onChange={field.onChange}
                                  presets={METRICS_TIME_RANGE_PRESETS}
                                  dataTestId="CreateSupportBundlePage-MetricsTimeRange"
                                />
                              )}
                            />
                          </NestedSettings>
                        )}
                      </CategoryRow>
                    </CategoryList>
                  </Box>
                )}
              </Box>
            </RadioCard>
            <RadioCard
              title={t('customize.title')}
              description={t('customize.description')}
              selected={values.mode === CollectionMode.CUSTOMIZE}
              expanded={expandedCard === CollectionMode.CUSTOMIZE}
              disabled
              onSelect={() => selectMode(CollectionMode.CUSTOMIZE)}
              onToggleExpanded={() => toggleExpanded(CollectionMode.CUSTOMIZE)}
              dataTestId="CreateSupportBundlePage-Customize"
            />
          </Section>
        </Box>
        <SummaryPanel
          categories={selectedCategories}
          summary={summary}
          isEstimating={estimate.isEstimating}
          asOf={estimate.asOf}
          onRefreshEstimate={() => setEstimateNow(new Date())}
          onScopeToFewerNodes={scopeToFewerNodes}
          onCreate={onCreate}
          isCreateDisabled={
            !estimateSpec || selectedComponents.length === 0 || createBundle.isLoading
          }
          isCreating={createBundle.isLoading}
        />
      </Box>
    </Dialog>
  );
};
