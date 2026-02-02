import { forwardRef, useContext, useImperativeHandle } from 'react';
import { useTranslation } from 'react-i18next';
import { yupResolver } from '@hookform/resolvers/yup';
import { FormProvider, useForm } from 'react-hook-form';
import { mui } from '@yugabyte-ui-library/core';
import { AssignPublicIPField, EARField, EITField } from '../../fields';
import { StyledPanel, StyledHeader, StyledContent } from '../../components/DefaultComponents';
import {
  CreateUniverseContext,
  CreateUniverseContextMethods,
  StepsRef
} from '../../CreateUniverseContext';
import { SecurityValidationSchema } from './ValidationSchema';
import { CloudType } from '@app/redesign/features/universe/universe-form/utils/dto';
import { SecuritySettingsProps } from './dtos';

const { Box } = mui;

export const SecuritySettings = forwardRef<StepsRef>((_, forwardRef) => {
  const [
    { securitySettings, generalSettings },
    { moveToNextPage, moveToPreviousPage, saveSecuritySettings }
  ] = (useContext(CreateUniverseContext) as unknown) as CreateUniverseContextMethods;

  const provider = generalSettings?.providerConfiguration;

  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.securitySettings'
  });

  const methods = useForm<SecuritySettingsProps>({
    resolver: yupResolver(SecurityValidationSchema()),
    defaultValues: { ...securitySettings },
    mode: 'onChange'
  });

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () => {
        return methods.handleSubmit((data) => {
          saveSecuritySettings(data);
          moveToNextPage();
        })();
      },
      onPrev: () => {
        moveToPreviousPage();
      }
    }),
    []
  );

  return (
    <FormProvider {...methods}>
      <Box sx={{ display: 'flex', flexDirection: 'column', width: '100%', gap: '24px' }}>
        {provider?.code !== CloudType.kubernetes && (
          <StyledPanel>
            <StyledHeader>{t('publicIPTitle')}</StyledHeader>
            <StyledContent>
              <AssignPublicIPField
                disabled={false}
                providerCode={generalSettings?.providerConfiguration?.code ?? ''}
              />
            </StyledContent>
          </StyledPanel>
        )}
        <StyledPanel>
          <StyledHeader>{t('eitTitle')}</StyledHeader>
          <StyledContent>
            <EITField disabled={false} />
          </StyledContent>
        </StyledPanel>
        <StyledPanel>
          <StyledHeader>{t('earTitle')}</StyledHeader>
          <StyledContent>
            <EARField disabled={false} />
          </StyledContent>
        </StyledPanel>
      </Box>
    </FormProvider>
  );
});

SecuritySettings.displayName = 'SecuritySettings';
