import { useContext, useState } from 'react';
import { Trans, useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import pluralize from 'pluralize';
import { mui } from '@yugabyte-ui-library/core';
import { FaultToleranceTypeField, ReplicationFactorField } from '../../fields';
import { ResilienceTooltip } from './index';
import { FaultToleranceType, ResilienceAndRegionsProps } from './dtos';
import { FAULT_TOLERANCE_TYPE, RESILIENCE_FACTOR } from '../../fields/FieldNames';
import { ResilienceRequirementCard } from './ResilienceRequirementCard';
import { CreateUniverseContext, CreateUniverseContextMethods } from '../../CreateUniverseContext';
import { CloudType } from '@app/redesign/helpers/dtos';

const { Box, styled, Typography } = mui;

const Link = styled('span')(() => ({
  textDecorationLine: 'underline',
  textDecorationStyle: 'dotted',
  cursor: 'pointer'
}));

const Root = styled(Box)(({ theme }) => ({
  padding: '24px',
  border: `1px solid ${theme.palette.grey[200]}`,
  borderRadius: '8px',
  gap: '24px',
  display: 'flex',
  flexDirection: 'column'
}));

const ResilienceHeader = styled(Typography)(({ theme }) => ({
  color: '#0B1117',
  fontSize: '15px',
  fontWeight: 600,
  lineHeight: '16px'
}));

const ChooseResilienceCard = styled(Box)(({ theme }) => ({
  padding: '32px 24px',
  display: 'flex',
  gap: '32px',
  border: `1px solid ${theme.palette.grey[200]}`,
  borderRadius: '8px',
  flexDirection: 'column'
}));

const NoneCautionRow = styled(Box)(() => ({
  display: 'flex',
  gap: '8px',
  alignItems: 'center'
}));

const CautionBadge = styled(Box)(() => ({
  backgroundColor: '#FFEEC8',
  borderRadius: '6px',
  padding: '4px 6px',
  fontSize: '11.5px',
  fontWeight: 500,
  color: '#9D6C00',
  whiteSpace: 'nowrap',
  lineHeight: '16px'
}));

/** Segmented RF control width used for the None collapse/expand slide. */
const RF_SEGMENT_MAX_WIDTH_PX = 130;

const RfSegmentCollapse = styled(Box, {
  shouldForwardProp: (prop) => prop !== 'collapsed'
})<{ collapsed: boolean }>(({ collapsed }) => ({
  maxWidth: collapsed ? 0 : RF_SEGMENT_MAX_WIDTH_PX,
  opacity: collapsed ? 0 : 1,
  marginRight: collapsed ? '-8px' : 0,
  paddingInline: collapsed ? 0 : '1px',
  overflow: 'hidden',
  flexShrink: 0,
  pointerEvents: collapsed ? 'none' : 'auto',
  // Speeds from Figma Make microinteraction (left-side timing values).
  transition: collapsed
    ? [
        'max-width 568ms cubic-bezier(0.4, 0, 0.2, 1)',
        'opacity 320ms cubic-bezier(0.4, 0, 1, 1) 0ms',
        'margin-right 568ms cubic-bezier(0.4, 0, 0.2, 1)'
      ].join(', ')
    : [
        'max-width 640ms cubic-bezier(0.2, 0, 0, 1)',
        'opacity 427ms cubic-bezier(0, 0, 0.2, 1) 177ms',
        'margin-right 640ms cubic-bezier(0.2, 0, 0, 1)'
      ].join(', ')
}));

const CautionCollapse = styled(Box, {
  shouldForwardProp: (prop) => prop !== 'expanded'
})<{ expanded: boolean }>(({ expanded }) => ({
  maxHeight: expanded ? 80 : 0,
  opacity: expanded ? 1 : 0,
  overflow: 'hidden',
  // Cancel one flex gap so a zero-height caution row doesn't add extra space.
  marginTop: expanded ? 0 : '-32px',
  transition: expanded
    ? [
        'max-height 500ms cubic-bezier(0.2, 0, 0, 1)',
        'opacity 300ms cubic-bezier(0, 0, 0.2, 1) 100ms',
        'margin-top 500ms cubic-bezier(0.2, 0, 0, 1)'
      ].join(', ')
    : [
        'max-height 400ms cubic-bezier(0.4, 0, 0.2, 1)',
        'opacity 200ms cubic-bezier(0.4, 0, 1, 1) 0ms',
        'margin-top 400ms cubic-bezier(0.4, 0, 0.2, 1)'
      ].join(', ')
}));

export const GuidedMode = () => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.resilienceAndRegions.guidedMode'
  });
  const [showResilienceTooltip, setShowResilienceTooltip] = useState(false);
  const { watch, getValues } = useFormContext<ResilienceAndRegionsProps>();
  const [{ generalSettings }] = useContext(
    CreateUniverseContext
  ) as unknown as CreateUniverseContextMethods;
  const isK8s =
    generalSettings?.cloud === CloudType.kubernetes ||
    generalSettings?.providerConfiguration?.code === CloudType.kubernetes;
  const resilienceFactor = watch(RESILIENCE_FACTOR);
  const faultToleranceType = watch(FAULT_TOLERANCE_TYPE);
  const isNone = faultToleranceType === FaultToleranceType.NONE;

  return (
    <Root>
      <ResilienceHeader>{t('header')}</ResilienceHeader>
      <Typography
        variant="body2"
        sx={(theme) => ({ fontWeight: 500, lineHeight: '16px', color: theme.palette.grey[900] })}
      >
        <Trans
          i18nKey="helpText"
          t={t}
          components={{
            a: (
              <Link
                onClick={() => {
                  setShowResilienceTooltip(true);
                }}
              />
            )
          }}
        />
      </Typography>
      <ResilienceTooltip
        open={showResilienceTooltip}
        onClose={() => {
          setShowResilienceTooltip(false);
        }}
      />
      <ChooseResilienceCard>
        <Box
          sx={{
            display: 'flex',
            flexDirection: 'row',
            gap: '8px',
            alignItems: 'center',
            flexWrap: 'nowrap',
            '.yb-MuiFormControlLabel-root': { marginBottom: '0 !important' }
          }}
        >
          {t('resilientTo')}
          <RfSegmentCollapse collapsed={isNone} data-testid="guided-rf-segment-collapse">
            <ReplicationFactorField
              hideLabel
              replication_options={['1', '2', '3']}
              fieldName={RESILIENCE_FACTOR}
            />
          </RfSegmentCollapse>
          <FaultToleranceTypeField
            name={FAULT_TOLERANCE_TYPE}
            label=""
            t={t}
            isK8s={isK8s}
            sx={{ width: '200px', minWidth: '200px', flexShrink: 0 }}
          />
          {!isNone && <> {pluralize(t('resilienceOutageWord'), resilienceFactor)}.</>}
        </Box>
        <CautionCollapse expanded={isNone}>
          <NoneCautionRow>
            <CautionBadge>{t('cautionLabel')}</CautionBadge>
            <Typography
              variant="body2"
              sx={{ color: '#4E5F6D', fontSize: '13px', lineHeight: '19px', fontWeight: 400 }}
            >
              {t('noneResilienceCautionMsg')}
            </Typography>
          </NoneCautionRow>
        </CautionCollapse>
        <ResilienceRequirementCard
          resilienceAndRegionsProps={getValues()}
          placementStep="resilience"
        />
      </ChooseResilienceCard>
    </Root>
  );
};
