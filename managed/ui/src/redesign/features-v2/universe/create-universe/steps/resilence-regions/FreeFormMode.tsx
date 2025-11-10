import { useFormContext } from 'react-hook-form';
import { Trans, useTranslation } from 'react-i18next';
import { mui, YBTag } from '@yugabyte-ui-library/core';
import { Collapse, styled, Typography } from '@material-ui/core';
import { ResilienceAndRegionsProps } from './dtos';
import { ReplicationFactorField } from '../../fields/replication-factor/ReplicationFactorField';
import { ResilienceTooltip } from './ResilienceTooltip';
import { useState } from 'react';

const { Box } = mui;

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
      <Collapse in={replicationFactor === 1}>
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
            {t('guidedMode.faultToleranceNone.caution')}
          </YBTag>
          {t('freeFormFaultToleranceMinMsg')}
        </Box>
      </Collapse>
      <ResilienceTooltip
        open={showResilienceTooltip}
        onClose={() => {
          setShowResilienceTooltip(false);
        }}
      />
    </Box>
  );
};
