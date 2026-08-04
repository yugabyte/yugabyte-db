import { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { mui, YBCheckboxField } from '@yugabyte-ui-library/core';
import { FieldContainer } from '../../components/DefaultComponents';
import { OtherAdvancedProps } from '../../steps/advanced-settings/dtos';

const { Typography, styled } = mui;

interface IPV6FieldProps {
  disabled?: boolean;
}

const IPV6_FIELD = 'enableIPV6';

const StyledSubText = styled(Typography)(({ theme }) => ({
  fontFamily: 'Inter',
  fontSize: 11.5,
  fontWeight: 400,
  color: ' #4E5F6D',
  lineHeight: '16px',
  marginLeft: theme.spacing(5.5)
}));

export const IPV6Field: FC<IPV6FieldProps> = ({ disabled = false }) => {
  const { control } = useFormContext<OtherAdvancedProps>();
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.otherAdvancedSettings.ipv6Field'
  });

  return (
    <FieldContainer sx={{ padding: '16px 24px' }}>
      <YBCheckboxField
        dataTestId="ipv6-field"
        name={IPV6_FIELD}
        control={control}
        label={t('label')}
        size="large"
        disabled={disabled}
      />
      <StyledSubText>{t('subText')}</StyledSubText>
    </FieldContainer>
  );
};
