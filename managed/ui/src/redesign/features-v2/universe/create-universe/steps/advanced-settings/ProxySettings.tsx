import { forwardRef, useContext, useEffect, useImperativeHandle, useMemo, useState } from 'react';
import { isEmpty } from 'lodash';
import { Trans, useTranslation } from 'react-i18next';
import { FormProvider, useForm } from 'react-hook-form';
import { yupResolver } from '@hookform/resolvers/yup';
import { AlertVariant, YBAlert, mui } from '@yugabyte-ui-library/core';
import { EnableProxyServer } from '@app/redesign/features-v2/universe/create-universe/fields';
import {
  StyledContent,
  StyledHeader,
  StyledPanel
} from '@app/redesign/features-v2/universe/create-universe/components/DefaultComponents';
import {
  CreateUniverseContext,
  CreateUniverseContextMethods,
  initialCreateUniverseFormState,
  StepsRef
} from '@app/redesign/features-v2/universe/create-universe/CreateUniverseContext';
import { ProxyAdvancedProps } from '@app/redesign/features-v2/universe/create-universe/steps/advanced-settings/dtos';
import { ProxySettingsValidationSchema } from '@app/redesign/features-v2/universe/create-universe/steps/advanced-settings/ProxySettingsValidationSchema';

const { Box } = mui;

export const ProxySettings = forwardRef<StepsRef>((_, forwardRef) => {
  const [{ proxySettings }, { moveToNextPage, moveToPreviousPage, saveProxySettings }] = useContext(
    CreateUniverseContext
  ) as unknown as CreateUniverseContextMethods;

  const { t } = useTranslation();
  const [showErrorsAfterSubmit, setShowErrorsAfterSubmit] = useState(false);

  const validationSchema = useMemo(() => ProxySettingsValidationSchema(t), [t]);

  const methods = useForm<ProxyAdvancedProps>({
    defaultValues: proxySettings ?? initialCreateUniverseFormState.proxySettings,
    resolver: yupResolver(validationSchema),
    mode: 'onSubmit',
    reValidateMode: 'onChange',
    criteriaMode: 'all'
  });

  const {
    formState: { errors, isSubmitted },
    trigger,
    watch
  } = methods;

  const hasErrors = !isEmpty(errors);

  useEffect(() => {
    if (isSubmitted && !hasErrors) {
      setShowErrorsAfterSubmit(false);
    }
  }, [isSubmitted, hasErrors]);

  const watched = watch();
  useEffect(() => {
    if (isSubmitted) trigger();
  }, [JSON.stringify(watched), isSubmitted, trigger]);

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () => {
        setShowErrorsAfterSubmit(true);
        return methods.handleSubmit((data) => {
          saveProxySettings(data);
          moveToNextPage();
        })();
      },
      onPrev: () => {
        saveProxySettings(methods.getValues());
        moveToPreviousPage();
      }
    }),
    [methods, saveProxySettings, moveToNextPage, moveToPreviousPage]
  );

  return (
    <FormProvider {...methods}>
      <StyledPanel>
        <StyledHeader>{t('createUniverseV2.proxySettings.header')}</StyledHeader>
        <StyledContent>
          <EnableProxyServer disabled={false} />
        </StyledContent>
      </StyledPanel>
      {showErrorsAfterSubmit && hasErrors && (
        <Box>
          <YBAlert
            open
            variant={AlertVariant.Error}
            text={
              <Trans
                t={t}
                i18nKey={'createUniverseV2.proxySettings.validation.globalNextError'}
                components={{ strong: <Box component="span" sx={{ fontWeight: 600 }} /> }}
              />
            }
          />
        </Box>
      )}
    </FormProvider>
  );
});

ProxySettings.displayName = 'ProxySettings';
