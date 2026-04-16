import { FC } from 'react';
import { useTranslation, Trans } from 'react-i18next';
import { mui, YBToggleField } from '@yugabyte-ui-library/core';
import { RRDatabaseSettingsProps } from './dtos';
import { useFormContext } from 'react-hook-form';

const { Box, Typography, Link, styled } = mui;

export const CUSTOMIZE_RR_FLAG_FIELD = 'customizeRRFlags';

const StyledSubText = styled(Typography)({
  fontSize: '11.5px',
  lineHeight: '16px',
  fontWeight: 400,
  color: '#67666C',
  marginLeft: '8px'
});

export const CustomizeRRFlagField = () => {
  const { control } = useFormContext<RRDatabaseSettingsProps>();
  const { t } = useTranslation('translation', { keyPrefix: 'readReplica.addRR' });

  return (
    <Box
      sx={{
        display: 'flex',
        width: '548px',
        backgroundColor: '#FBFCFD',
        border: '1px solid #D7DEE4',
        borderRadius: '8px',
        padding: '16px 24px',
        alignItems: 'start'
      }}
      data-testid="InheritFlag-Container"
    >
      <YBToggleField
        name={CUSTOMIZE_RR_FLAG_FIELD}
        inputProps={{
          'data-testid': 'PGCompatibiltyField-Toggle'
        }}
        control={control}
        dataTestId="inheritFlag-field"
      />
      <Box sx={{ display: 'flex', flexDirection: 'column', width: '100%' }}>
        <Box sx={{ display: 'flex', flexDirection: 'row', alignItems: 'center' }}>
          <Typography variant="body2" sx={{ color: '#0B1117', ml: 1, mr: 1 }}>
            {t('advancedFlagLabel')}
          </Typography>
        </Box>

        <Box sx={{ gap: '8px' }}>
          <StyledSubText>
            {t('advancedFlagSubText')} <br />
            <Trans t={t} i18nKey={'advancedFlagNote'}></Trans>
          </StyledSubText>
        </Box>
      </Box>
    </Box>
  );
};
