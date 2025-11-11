import { useFormContext } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { styled, Typography } from '@material-ui/core';
import { mui, YBTag } from '@yugabyte-ui-library/core';
import { FaultToleranceTypeField } from '../../fields/fault-tolerance/FaultToleranceTypeField';
import { FAULT_TOLERANCE_TYPE } from '../../fields/FieldNames';
import { ReplicationFactorField } from '../../fields/replication-factor/ReplicationFactorField';
import {
  ReplicationStatusAvailabilityStatus,
  ReplicationStatusCard
} from '../nodes-availability/ReplicationStatusCard';
import { FaultToleranceType, ResilienceAndRegionsProps } from './dtos';
import { ResilienceTooltip } from './ResilienceTooltip';
import { useState } from 'react';
const { Box, Collapse } = mui;

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
      <div style={{ display: 'flex', gap: '8px', marginTop: '32px', alignItems: 'center' }}>
        <FaultToleranceTypeField name={FAULT_TOLERANCE_TYPE} label={t('faultTolerance')} t={t} />
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
            <YBTag
              size="medium"
              customSx={{ color: '#9D6C00', background: '#FFEEC8' }}
              color="warning"
            >
              {t('faultToleranceNone.caution')}
            </YBTag>
            {t('faultToleranceNone.msg')}
          </Box>
        </Collapse>
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
