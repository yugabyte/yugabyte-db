import { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { mui, YBCheckboxField } from '@yugabyte-ui-library/core';
import { SecuritySettingsProps } from '../../steps/security-settings/dtos';

const { Box, Typography, styled } = mui;

interface PublicIPFieldProps {
  disabled: boolean;
}

const ASSIGN_PUBLIC_IP_FIELD = 'assignPublicIP';

export const StyledSubText = styled(Typography)(({ theme }) => ({
  fontFamily: 'Inter',
  fontSize: 11.5,
  fontWeight: 400,
  color: ' #4E5F6D',
  lineHeight: '16px',
  marginLeft: theme.spacing(4)
}));

export const AssignPublicIPField: FC<PublicIPFieldProps> = ({ disabled }) => {
  const { control } = useFormContext<SecuritySettingsProps>();

  return (
    <Box
      sx={{
        display: 'flex',
        width: '548px',
        flexDirection: 'column',
        backgroundColor: '#FBFCFD',
        border: '1px solid #D7DEE4',
        borderRadius: '8px',
        padding: '16px 24px'
      }}
    >
      <YBCheckboxField
        name={ASSIGN_PUBLIC_IP_FIELD}
        control={control}
        label={'Assign Public IP'}
        size="large"
      />
      <StyledSubText>
        Assign a public IP to the DB servers for connections over the internet.
      </StyledSubText>
    </Box>
  );
};
