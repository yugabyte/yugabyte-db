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
import { useRuntimeConfigValues } from '@app/redesign/features-v2/universe/create-universe/helpers/utils';
import { getClusterByType } from '@app/redesign/features-v2/universe/edit-universe/EditUniverseUtils';
import { GFLAGS_FIELD } from '@app/redesign/features-v2/universe/create-universe/fields/FieldNames';
import { RunTimeConfigEntry } from '@app/redesign/features/universe/universe-form/utils/dto';
import { RuntimeConfigKey } from '@app/redesign/helpers/constants';
import {
  ClusterGFlagsAllOfGflagGroupsItem,
  ClusterSpecClusterType
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

const { Box } = mui;

export const RRDatabaseSettings = forwardRef<StepsRef>((_, forwardRef) => {
  const [
    { databaseSettings, universeData },
    { moveToNextPage, moveToPreviousPage, saveDatabaseSettings }
  ] = useContext(AddRRContext) as unknown as AddRRContextMethods;

  const { t } = useTranslation('translation', { keyPrefix: 'readReplica.addRR' });
  const { runtimeConfigs } = useRuntimeConfigValues();
  const methods = useForm<RRDatabaseSettingsProps>({
    defaultValues: databaseSettings,
    mode: 'onChange'
  });

  const { control, watch } = methods;

  const customizeFlagValue = watch(CUSTOMIZE_RR_FLAG_FIELD);

  const isGFlagMultilineConfEnabled =
    runtimeConfigs?.configEntries?.find(
      (c: RunTimeConfigEntry) => c.key === RuntimeConfigKey.IS_GFLAG_MULTILINE_ENABLED
    )?.value === 'true';

  const primaryCluster = universeData
    ? getClusterByType(universeData, ClusterSpecClusterType.PRIMARY)
    : undefined;
  const isPGSupported = !!primaryCluster?.gflags?.gflag_groups?.includes(
    ClusterGFlagsAllOfGflagGroupsItem.ENHANCED_POSTGRES_COMPATIBILITY
  );

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
                fieldPath={GFLAGS_FIELD}
                dbVersion={getDBVersion(universeData)}
                isReadReplica={true}
                editMode={false}
                isGFlagMultilineConfEnabled={isGFlagMultilineConfEnabled}
                isPGSupported={isPGSupported}
                isReadOnly={false}
              />
            )}
          </StyledContent>
        </StyledPanel>
      </Box>
    </FormProvider>
  );
});
