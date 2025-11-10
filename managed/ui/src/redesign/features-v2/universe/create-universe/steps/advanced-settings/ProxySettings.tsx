import { forwardRef, useContext, useImperativeHandle } from 'react';
import { FormProvider, useForm } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import {
  CreateUniverseContext,
  CreateUniverseContextMethods,
  StepsRef
} from '@app/redesign/features-v2/universe/create-universe/CreateUniverseContext';
import {
  StyledContent,
  StyledHeader,
  StyledPanel
} from '@app/redesign/features-v2/universe/create-universe/components/DefaultComponents';
import { ProxyAdvancedProps } from '@app/redesign/features-v2/universe/create-universe/steps/advanced-settings/dtos';
import { EnableProxyServer } from '@app/redesign/features-v2/universe/create-universe/fields';

export const ProxySettings = forwardRef<StepsRef>((_, forwardRef) => {
  const [
    { proxySettings },
    { moveToNextPage, moveToPreviousPage, saveProxySettings }
  ] = (useContext(CreateUniverseContext) as unknown) as CreateUniverseContextMethods;

  const { t } = useTranslation();

  const methods = useForm<ProxyAdvancedProps>({ defaultValues: proxySettings });

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () => {
        return methods.handleSubmit((data) => {
          saveProxySettings(data);
          moveToNextPage();
        })();
      },
      onPrev: () => {
        methods.handleSubmit((data) => {
          saveProxySettings(data);
          moveToPreviousPage();
        })();
      }
    }),
    []
  );

  return (
    <FormProvider {...methods}>
      <StyledPanel>
        <StyledHeader>{t('createUniverseV2.proxySettings.header')}</StyledHeader>
        <StyledContent>
          <EnableProxyServer disabled={false} />
        </StyledContent>
      </StyledPanel>
    </FormProvider>
  );
});

ProxySettings.displayName = 'ProxySettings';
