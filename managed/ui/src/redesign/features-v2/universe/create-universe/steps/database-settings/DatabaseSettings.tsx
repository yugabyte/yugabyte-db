import { forwardRef, useContext, useImperativeHandle } from 'react';
import {
  CreateUniverseContext,
  CreateUniverseContextMethods,
  StepsRef
} from '../../CreateUniverseContext';
import { FormProvider, useForm } from 'react-hook-form';
import { yupResolver } from '@hookform/resolvers/yup';
import { useTranslation } from 'react-i18next';
import { mui, YBAccordion } from '@yugabyte-ui-library/core';
import { FormHelperText } from '@material-ui/core';
import { DatabaseSettingsProps } from './dtos';
import { YCQField, YSQLField, ConnectionPoolingField, PGCompatibiltyField } from '../../fields';
import { StyledPanel, StyledHeader, StyledContent } from '../../components/DefaultComponents';
import { DEFAULT_COMMUNICATION_PORTS } from '../../helpers/constants';
import { GFlagsFieldNew } from '../../../../../features/universe/universe-form/form/fields/GflagsField/GflagsFieldNew';
import { DatabaseValidationSchema } from './ValidationSchema';

const { Box } = mui;

export const DatabaseSettings = forwardRef<StepsRef>((_, forwardRef) => {
  const [
    { databaseSettings, generalSettings },
    { moveToNextPage, moveToPreviousPage, saveDatabaseSettings }
  ] = (useContext(CreateUniverseContext) as unknown) as CreateUniverseContextMethods;

  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.databaseSettings'
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

  const { control } = methods;

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () => {
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
      <StyledPanel>
        <StyledHeader>{t('interface')}</StyledHeader>
        <StyledContent>
          <YSQLField />
          <YCQField />
          {methods?.formState.errors?.ysql?.enable && (
            <FormHelperText error>{methods.formState.errors.ysql.enable.message}</FormHelperText>
          )}
        </StyledContent>
      </StyledPanel>
      <Box sx={{ mt: 3 }}></Box>
      <StyledPanel>
        <StyledHeader>{t('features')}</StyledHeader>
        <StyledContent>
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
      <Box sx={{ mt: 3 }}></Box>
      <YBAccordion titleContent={t('advFlags')} sx={{ width: '100%' }}>
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
    </FormProvider>
  );
});

DatabaseSettings.displayName = 'DatabaseSettings';
