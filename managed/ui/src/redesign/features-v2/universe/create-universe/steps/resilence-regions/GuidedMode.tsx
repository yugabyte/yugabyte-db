import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { mui, YBSmartStatus, StatusType, IconPosition } from '@yugabyte-ui-library/core';
import { FaultToleranceTypeField, ReplicationFactorField } from '../../fields';
import { ReplicationStatusAvailabilityStatus, ReplicationStatusCard } from '../nodes-availability';
import { ResilienceTooltip } from './index';
import { FaultToleranceType, ResilienceAndRegionsProps } from './dtos';
import { FAULT_TOLERANCE_TYPE } from '../../fields/FieldNames';

const { Box, Collapse, styled, Typography } = mui;

const Link = styled('span')(({ theme }) => ({
  color: `${theme.palette.primary[600]}`,
  textDecoration: 'underline',
  cursor: 'pointer',
  '&:hover': {
    textDecoration: 'underline',
    color: `${theme.palette.primary[600]}`
  }
}));

export const GuidedMode = () => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.resilienceAndRegions.guidedMode'
  });
  const { watch } = useFormContext<ResilienceAndRegionsProps>();

  const faultToleranceType = watch(FAULT_TOLERANCE_TYPE);
  const [showResilienceTooltip, setShowResilienceTooltip] = useState(false);

  return (
    <Box
      sx={{
        padding: '24px',
        display: 'flex',
        flexDirection: 'column',
        border: '1px solid #D7DEE4',
        borderRadius: '8px'
      }}
    >
      <div>
        {t('instruction')} &nbsp;
        <Link
          onClick={() => {
            setShowResilienceTooltip(true);
          }}
          title={t('helpText')}
        >
          {t('helpText')}
        </Link>
      </div>
      <div style={{ display: 'flex', gap: '16px', marginTop: '32px', alignItems: 'center' }}>
        <FaultToleranceTypeField name={FAULT_TOLERANCE_TYPE} label={t('faultTolerance')} t={t} />
        {faultToleranceType !== FaultToleranceType.NONE && (
          <span style={{ color: '#D7DEE4', marginTop: '20px' }}>|</span>
        )}
        {faultToleranceType !== FaultToleranceType.NONE && (
          <div style={{ marginTop: '20px', display: 'flex', gap: '8px', alignItems: 'center' }}>
            <Typography variant="body2">{t('resilientTo')}</Typography>
            <ReplicationFactorField hideLabel replication_options={['1', '2', '3']} />
            <Typography variant="body2">
              {t(`${faultToleranceType}.name`)}
              {t('outages')}
            </Typography>
          </div>
        )}
      </div>
      <div style={{ marginTop: '16px', display: 'flex', flexDirection: 'column', gap: '16px' }}>
        <ReplicationStatusAvailabilityStatus />
        <ReplicationStatusCard hideSubText />
        {faultToleranceType === FaultToleranceType.NONE && (
          <Collapse in={faultToleranceType === FaultToleranceType.NONE}>
            <Box
              sx={{
                display: 'flex',
                flexDirection: 'row',
                gap: '8px',
                alignItems: 'center',
                color: '#4E5F6D'
              }}
            >
              <YBSmartStatus
                type={StatusType.WARNING}
                label={t('faultToleranceNone.caution')}
                iconPosition={IconPosition.NONE}
              />
              {t('faultToleranceNone.msg')}
            </Box>
          </Collapse>
        )}
      </div>
      <ResilienceTooltip
        open={showResilienceTooltip}
        onClose={() => {
          setShowResilienceTooltip(false);
        }}
      />
    </Box>
  );
};
