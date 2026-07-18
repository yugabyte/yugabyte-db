import { forwardRef, useContext, useImperativeHandle } from 'react';
import { useForm, FormProvider } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { mui } from '@yugabyte-ui-library/core';
import { RRBreadCrumbs } from '../../ReadReplicaBreadCrumbs';
import { StepsRef, AddRRContext, AddRRContextMethods } from '../../AddReadReplicaContext';
import { RRDatabaseSettingsProps } from './dtos';
import {
  StyledPanel,
  StyledHeader,
  StyledContent
} from '@app/redesign/features-v2/universe/create-universe/components/DefaultComponents';
import { CustomizeRRFlagField, CUSTOMIZE_RR_FLAG_FIELD } from './CustomizeRRFlagField';
import { GFlagsFieldNew } from '@app/redesign/features/universe/universe-form/form/fields/GflagsField/GflagsFieldNew';
import { getDBVersion } from '../../AddReadReplicaUtils';

const { Box } = mui;

export const RRDatabaseSettings = forwardRef<StepsRef>((_, forwardRef) => {
  const [
    { activeStep, databaseSettings, universeData },
    { moveToNextPage, moveToPreviousPage, saveDatabaseSettings }
  ] = (useContext(AddRRContext) as unknown) as AddRRContextMethods;

  const { t } = useTranslation('translation', { keyPrefix: 'readReplica.addRR' });
  const methods = useForm<RRDatabaseSettingsProps>({
    defaultValues: databaseSettings,
    mode: 'onChange'
  });

  const { control, watch } = methods;

  const customizeFlagValue = watch(CUSTOMIZE_RR_FLAG_FIELD);

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
      <Box sx={{ display: 'flex', flexDirection: 'column', gap: '24px' }}>
        <RRBreadCrumbs groupTitle={t('database')} subTitle={t('databaseSettings')} />
        <StyledPanel>
          <StyledHeader>{t('advancedFlags')}</StyledHeader>
          <StyledContent>
            <CustomizeRRFlagField />
            {universeData && customizeFlagValue && (
              <GFlagsFieldNew
                control={control}
                fieldPath={'gFlags'}
                dbVersion={getDBVersion(universeData)}
                isReadReplica={true}
                editMode={false}
                isGFlagMultilineConfEnabled={false}
                isPGSupported={false}
                isReadOnly={false}
              />
            )}
          </StyledContent>
        </StyledPanel>
      </Box>
    </FormProvider>
  );
});
