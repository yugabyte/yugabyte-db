import { forwardRef, useContext, useImperativeHandle } from 'react';
import {
  CreateUniverseContext,
  CreateUniverseContextMethods,
  StepsRef
} from '../../CreateUniverseContext';
import { FormProvider, useForm } from 'react-hook-form';
import { yupResolver } from '@hookform/resolvers/yup';
import { useTranslation } from 'react-i18next';
import { mui } from '@yugabyte-ui-library/core';
import { AssignPublicIPField, EARField, EITField } from '../../fields';
import { StyledPanel, StyledHeader, StyledContent } from '../../components/DefaultComponents';
import { SecuritySettingsProps } from './dtos';
import { SecurityValidationSchema } from './ValidationSchema';

const { Box } = mui;

export const SecuritySettings = forwardRef<StepsRef>((_, forwardRef) => {
  const [
    { securitySettings, generalSettings },
    { moveToNextPage, moveToPreviousPage, saveSecuritySettings }
  ] = (useContext(CreateUniverseContext) as unknown) as CreateUniverseContextMethods;

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
      <StyledPanel>
        <StyledHeader>{t('publicIPTitle')}</StyledHeader>
        <StyledContent>
          <AssignPublicIPField
            disabled={false}
            providerCode={generalSettings?.providerConfiguration?.code ?? ''}
          />
        </StyledContent>
      </StyledPanel>
      <Box sx={{ mt: 3 }}></Box>
      <StyledPanel>
        <StyledHeader>{t('eitTitle')}</StyledHeader>
        <StyledContent>
          <EITField disabled={false} />
        </StyledContent>
      </StyledPanel>
      <Box sx={{ mt: 3 }}></Box>
      <StyledPanel>
        <StyledHeader>{t('earTitle')}</StyledHeader>
        <StyledContent>
          <EARField disabled={false} />
        </StyledContent>
      </StyledPanel>
    </FormProvider>
  );
});

SecuritySettings.displayName = 'SecuritySettings';
