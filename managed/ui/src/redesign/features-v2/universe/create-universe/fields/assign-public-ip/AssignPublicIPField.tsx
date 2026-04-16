import { FC, useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { mui, YBCheckboxField } from '@yugabyte-ui-library/core';
import { FieldContainer } from '../../components/DefaultComponents';
import { SecuritySettingsProps } from '../../steps/security-settings/dtos';
import { CloudType } from '../../../../../helpers/dtos';

const { Typography, styled } = mui;

interface PublicIPFieldProps {
  disabled: boolean;
  providerCode: string;
}

const ASSIGN_PUBLIC_IP_FIELD = 'assignPublicIP';

export const StyledSubText = styled(Typography)(({ theme }) => ({
  fontFamily: 'Inter',
  fontSize: 11.5,
  fontWeight: 400,
  color: ' #4E5F6D',
  lineHeight: '16px',
  marginLeft: theme.spacing(5.5)
}));

export const AssignPublicIPField: FC<PublicIPFieldProps> = ({ disabled, providerCode }) => {
  const { control, setValue } = useFormContext<SecuritySettingsProps>();
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.securitySettings.publicIPField'
  });

  useEffect(() => {
    providerCode === CloudType.azu
      ? setValue(ASSIGN_PUBLIC_IP_FIELD, false)
      : setValue(ASSIGN_PUBLIC_IP_FIELD, true);
  }, [providerCode]);

  return (
    <FieldContainer sx={{ padding: '16px 24px' }}>
      <YBCheckboxField
        dataTestId="assign-public-ip-field"
        name={ASSIGN_PUBLIC_IP_FIELD}
        control={control}
        label={t('label')}
        size="large"
      />
      <StyledSubText>{t('subText')}</StyledSubText>
    </FieldContainer>
  );
};
