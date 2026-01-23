import { useState } from 'react';
import { useFormContext } from 'react-hook-form';
import { Trans, useTranslation } from 'react-i18next';
import { mui, YBSmartStatus, StatusType, IconPosition } from '@yugabyte-ui-library/core';
import { ReplicationFactorInfoModal } from '../nodes-availability';
import { ReplicationFactorField } from '../../fields';
import { ResilienceAndRegionsProps } from './dtos';

const { Box, Collapse, styled, Typography } = mui;

const StyledLink = styled('a')(({ theme }) => ({
  color: `${theme.palette.primary[600]}`,
  textDecoration: 'underline',
  cursor: 'pointer',
  '&:hover': {
    textDecoration: 'underline',
    color: `${theme.palette.primary[600]}`
  }
}));

export const FreeFormMode = () => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.resilienceAndRegions'
  });

  const { watch } = useFormContext<ResilienceAndRegionsProps>();
  const replicationFactor = watch('replicationFactor');
  const [showResilienceTooltip, setShowResilienceTooltip] = useState(false);

  return (
    <Box
      sx={{
        padding: '24px',
        border: '1px solid #D7DEE4',
        borderRadius: '8px',
        display: 'flex',
        gap: '32px',
        flexDirection: 'column'
      }}
    >
      <Typography variant="body2">
        <Trans
          t={t}
          i18nKey="freeFormHelpText"
          components={{
            a: (
              <StyledLink
                onClick={() => {
                  setShowResilienceTooltip(true);
                }}
              />
            )
          }}
        />
      </Typography>
      <ReplicationFactorField />
      {replicationFactor === 1 && (
        <Collapse in={replicationFactor === 1}>
          <Box
            sx={{
              display: 'flex',
              flexDirection: 'row',
              gap: '8px',
              alignItems: 'center',
              color: '#4E5F6D',
              marginTop: '-16px'
            }}
          >
            <YBSmartStatus
              type={StatusType.WARNING}
              label={t('guidedMode.faultToleranceNone.caution')}
              iconPosition={IconPosition.NONE}
            />
            {t('freeFormFaultToleranceMinMsg')}
          </Box>
        </Collapse>
      )}
      <ReplicationFactorInfoModal
        open={showResilienceTooltip}
        onClose={() => {
          setShowResilienceTooltip(false);
        }}
      />
    </Box>
  );
};
