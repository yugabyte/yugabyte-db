import { useState } from 'react';
import { Trans, useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import pluralize from 'pluralize';
import { mui } from '@yugabyte-ui-library/core';
import { FaultToleranceTypeField, ReplicationFactorField } from '../../fields';
import { ResilienceTooltip } from './index';
import { FaultToleranceType, ResilienceAndRegionsProps } from './dtos';
import { FAULT_TOLERANCE_TYPE, RESILIENCE_FACTOR } from '../../fields/FieldNames';
import { ResilienceRequirementCard } from './ResilienceRequirementCard';

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

export const GuidedMode = () => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.resilienceAndRegions.guidedMode'
  });
  const [showResilienceTooltip, setShowResilienceTooltip] = useState(false);
  const { watch, getValues } = useFormContext<ResilienceAndRegionsProps>();
  const resilienceFactor = watch(RESILIENCE_FACTOR);
  const faultToleranceType = watch(FAULT_TOLERANCE_TYPE);
  const isNone = faultToleranceType === FaultToleranceType.NONE;

  return (
    <Root>
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
            flexWrap: 'wrap',
            '.yb-MuiFormControlLabel-root': { marginBottom: '0 !important' }
          }}
        >
          {t('resilientTo')}
          <ReplicationFactorField
            hideLabel
            replication_options={['1', '2', '3']}
            fieldName={RESILIENCE_FACTOR}
            segmentDisabled={isNone}
          />
          <FaultToleranceTypeField
            name={FAULT_TOLERANCE_TYPE}
            label=""
            t={t}
            sx={{ width: '160px', minWidth: '160px' }}
          />
          {!isNone && (
            <>
              {' '}
              {pluralize(t('resilienceOutageWord'), resilienceFactor)}.
            </>
          )}
        </Box>
        {isNone && (
          <NoneCautionRow>
            <CautionBadge>{t('cautionLabel')}</CautionBadge>
            <Typography
              variant="body2"
              sx={{ color: '#4E5F6D', fontSize: '13px', lineHeight: '19px', fontWeight: 400 }}
            >
              {t('noneResilienceCautionMsg')}
            </Typography>
          </NoneCautionRow>
        )}
        <ResilienceRequirementCard resilienceAndRegionsProps={getValues()} placementStep="resilience" />
      </ChooseResilienceCard>
    </Root>
  );
};
