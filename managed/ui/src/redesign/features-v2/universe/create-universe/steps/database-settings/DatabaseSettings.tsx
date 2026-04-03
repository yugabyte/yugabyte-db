import { forwardRef, useContext, useImperativeHandle, useEffect, useState } from 'react';
import { useTranslation, Trans } from 'react-i18next';
import { useUpdateEffect } from 'react-use';
import { FormProvider, useForm } from 'react-hook-form';
import { yupResolver } from '@hookform/resolvers/yup';
import { mui, YBAccordion, YBAlert, AlertVariant } from '@yugabyte-ui-library/core';
import { YCQField, YSQLField, ConnectionPoolingField, PGCompatibiltyField } from '../../fields';
import { StyledPanel, StyledHeader, StyledContent } from '../../components/DefaultComponents';
import { GFlagsFieldNew } from '../../../../../features/universe/universe-form/form/fields/GflagsField/GflagsFieldNew';
import { DatabaseValidationSchema } from './ValidationSchema';
import {
  CreateUniverseContext,
  CreateUniverseContextMethods,
  StepsRef
} from '../../CreateUniverseContext';
import { DatabaseSettingsProps } from './dtos';
import { DEFAULT_COMMUNICATION_PORTS } from '../../helpers/constants';
import {
  YSQL_FIELD,
  YCQL_FIELD,
  YSQL_CONFIRM_PWD,
  YCQL_CONFIRM_PWD
} from '../../fields/FieldNames';

//icons
import ErrorIcon from '../../../../../assets/error-new.svg';

const { Box, styled, Typography } = mui;

export const StyledError = styled(Typography)(({ theme }) => ({
  fontSize: 11.5,
  color: theme.palette.error[500],
  fontWeight: 400,
  lineHeight: '16px'
}));

export const DatabaseSettings = forwardRef<StepsRef>((_, forwardRef) => {
  const [
    { databaseSettings, generalSettings },
    { moveToNextPage, moveToPreviousPage, saveDatabaseSettings }
  ] = (useContext(CreateUniverseContext) as unknown) as CreateUniverseContextMethods;

  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2'
  });

  const methods = useForm<DatabaseSettingsProps>({
    resolver: yupResolver(DatabaseValidationSchema()),
    defaultValues: {
      ysqlServerRpcPort: DEFAULT_COMMUNICATION_PORTS.ysqlServerRpcPort,
      internalYsqlServerRpcPort: DEFAULT_COMMUNICATION_PORTS.internalYsqlServerRpcPort,
      ...databaseSettings
    },
    mode: 'onChange'
  });

  const [showErrorsAfterSubmit, setShowErrorsAfterSubmit] = useState(false);
  const { trigger, formState, watch, control, setError, clearErrors } = methods;
  const { errors, isSubmitted } = formState;

  const enableYSQLVal = watch(YSQL_FIELD);
  const enableYCQLVal = watch(YCQL_FIELD);
  const ysqlConfirmPwd = watch(YSQL_CONFIRM_PWD);
  const ycqlConfirmPwd = watch(YCQL_CONFIRM_PWD);

  useUpdateEffect(() => {
    if (!enableYCQLVal && !enableYSQLVal) {
      setError(YSQL_FIELD, {
        type: 'custom',
        message: 'You must select at least one API interface.'
      });
    } else clearErrors(YSQL_FIELD);
  }, [enableYSQLVal, enableYCQLVal]);

  useUpdateEffect(() => {
    if (isSubmitted) {
      trigger().then((isValid) => {
        if (isValid) setShowErrorsAfterSubmit(false);
      });
    }
  }, [ysqlConfirmPwd, ycqlConfirmPwd]);

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () => {
        setShowErrorsAfterSubmit(true);
        return methods.handleSubmit((data) => {
          saveDatabaseSettings(data);
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
        <StyledPanel>
          <StyledHeader>{t('databaseSettings.interface')}</StyledHeader>
          <StyledContent sx={{ gap: '16px' }}>
            {errors?.ysql?.enable?.message && showErrorsAfterSubmit && (
              <StyledError>
                <ErrorIcon />
                &nbsp;{errors?.ysql?.enable?.message}
              </StyledError>
            )}
            <YSQLField />
            <YCQField />
          </StyledContent>
        </StyledPanel>
        <StyledPanel>
          <StyledHeader>{t('databaseSettings.features')}</StyledHeader>
          <StyledContent sx={{ gap: '16px' }}>
            <ConnectionPoolingField
              disabled={false}
              dbVersion={generalSettings?.databaseVersion ?? ''}
            />
            <PGCompatibiltyField
              disabled={false}
              dbVersion={generalSettings?.databaseVersion ?? ''}
            />
          </StyledContent>
        </StyledPanel>
        <YBAccordion titleContent={t('databaseSettings.advFlags')} sx={{ width: '100%' }}>
          <GFlagsFieldNew
            control={control}
            fieldPath={'gFlags'}
            dbVersion={generalSettings?.databaseVersion ?? ''}
            isReadReplica={false}
            editMode={false}
            isGFlagMultilineConfEnabled={false}
            isPGSupported={false}
            isReadOnly={false}
          />
        </YBAccordion>
      </Box>
      {showErrorsAfterSubmit && errors && (
        <Box>
          <YBAlert
            open
            variant={AlertVariant.Error}
            text={<Trans t={t}>{t('validation.alertMsg')}</Trans>}
          />
        </Box>
      )}
    </FormProvider>
  );
});

DatabaseSettings.displayName = 'DatabaseSettings';
