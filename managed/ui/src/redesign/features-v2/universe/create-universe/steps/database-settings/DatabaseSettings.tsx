import { forwardRef, useContext, useImperativeHandle } from 'react';
import { useTranslation } from 'react-i18next';
import { FormProvider, useForm } from 'react-hook-form';
import { yupResolver } from '@hookform/resolvers/yup';
import { mui, YBAccordion } from '@yugabyte-ui-library/core';
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
      <Box sx={{ display: 'flex', flexDirection: 'column', width: '100%', gap: '24px' }}>
        <StyledPanel>
          <StyledHeader>{t('interface')}</StyledHeader>
          <StyledContent sx={{ gap: '16px' }}>
            <YSQLField />
            <YCQField />
          </StyledContent>
        </StyledPanel>
        <StyledPanel>
          <StyledHeader>{t('features')}</StyledHeader>
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
      </Box>
    </FormProvider>
  );
});

DatabaseSettings.displayName = 'DatabaseSettings';
