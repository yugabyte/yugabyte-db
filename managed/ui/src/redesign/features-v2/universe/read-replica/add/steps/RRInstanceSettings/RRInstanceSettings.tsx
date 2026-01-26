import { forwardRef, useContext, useEffect, useImperativeHandle } from 'react';
import { useForm, FormProvider } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { mui, YBCheckboxField } from '@yugabyte-ui-library/core';
import { RRBreadCrumbs } from '../../ReadReplicaBreadCrumbs';
import { StepsRef, AddRRContext, AddRRContextMethods } from '../../AddReadReplicaContext';
import { InstanceSettingProps } from '@app/redesign/features-v2/universe/create-universe/steps/hardware-settings/dtos';
import {
  StyledPanel,
  StyledHeader,
  StyledContent
} from '@app/redesign/features-v2/universe/create-universe/components/DefaultComponents';
import { InstanceBox } from '@app/redesign/features-v2/universe/create-universe/steps';
import {
  InstanceTypeField,
  VolumeInfoField
} from '@app/redesign/features-v2/universe/create-universe/fields';
import { useRuntimeConfigValues } from '@app/redesign/features-v2/universe/create-universe/helpers/utils';
import { getClusterByType } from '@app/redesign/features-v2/universe/edit-universe/EditUniverseUtils';
import { ClusterSpecClusterType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { CloudType } from '@app/redesign/features/universe/universe-form/utils/dto';
import { ProviderType } from '@app/redesign/features-v2/universe/create-universe/steps/general-settings/dtos';
import { StorageType } from '@app/redesign/helpers/dtos';

const { Box, styled, CircularProgress } = mui;

const StyledPanelWrapper = styled('div')(({ theme }) => ({
  display: 'flex',
  width: '734px',
  flexDirection: 'column',
  backgroundColor: '#FBFCFD',
  border: `1px solid ${theme.palette.grey[300]}`,
  borderRadius: '8px',
  padding: '24px'
}));

export const SAME_AS_PRIMARY_INST_FIELD = 'inheritPrimaryInstance';

export type RRInstanceSettingsProps = Partial<InstanceSettingProps> & {
  inheritPrimaryInstance: boolean;
};

export const RRInstanceSettings = forwardRef<StepsRef>((_, forwardRef) => {
  const [
    { instanceSettings, universeData },
    { moveToNextPage, moveToPreviousPage, saveInstanceSettings }
  ] = (useContext(AddRRContext) as unknown) as AddRRContextMethods;

  const primaryCluster = getClusterByType(universeData!, ClusterSpecClusterType.PRIMARY);

  const provider: Partial<ProviderType> = {
    uuid: primaryCluster?.provider_spec.provider ?? '',
    code: (primaryCluster?.placement_spec?.cloud_list[0].code ?? '') as CloudType
  };

  const {
    maxVolumeCount,
    isRuntimeConfigLoading,
    isProviderRuntimeConfigLoading
  } = useRuntimeConfigValues(provider.uuid);

  const { t } = useTranslation('translation');

  const methods = useForm<RRInstanceSettingsProps>({
    defaultValues: instanceSettings,
    mode: 'onChange'
  });

  const { control, watch, reset } = methods;

  const sameAsPrimary = watch(SAME_AS_PRIMARY_INST_FIELD);

  // Reset form to initial values from primary cluster when sameAsPrimary is true
  useEffect(() => {
    if (sameAsPrimary && primaryCluster && universeData) {
      const storageSpec = primaryCluster?.node_spec.storage_spec;
      const initialInstanceSettings: RRInstanceSettingsProps = {
        inheritPrimaryInstance: true,
        arch: universeData?.info?.arch,
        instanceType: primaryCluster?.node_spec.instance_type ?? null,
        useSpotInstance: primaryCluster?.use_spot_instance ?? false,
        deviceInfo: storageSpec
          ? {
              volumeSize: storageSpec?.volume_size,
              numVolumes: storageSpec?.num_volumes,
              diskIops: storageSpec?.disk_iops ?? null,
              throughput: storageSpec?.throughput ?? null,
              storageClass: 'standard',
              storageType: (storageSpec?.storage_type as StorageType) ?? null
            }
          : null
      };
      reset(initialInstanceSettings);
    }
  }, [sameAsPrimary, primaryCluster, universeData, reset]);

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () => {
        return methods.handleSubmit((data) => {
          moveToNextPage();
          saveInstanceSettings(data);
        })();
      },
      onPrev: () => {
        return methods.handleSubmit((data) => {
          moveToPreviousPage();
          saveInstanceSettings(data);
        })();
      }
    }),
    []
  );

  return (
    <FormProvider {...methods}>
      <Box sx={{ display: 'flex', flexDirection: 'column', gap: '24px' }}>
        <RRBreadCrumbs
          groupTitle={t('readReplica.addRR.hardware')}
          subTitle={t('readReplica.addRR.instanceOptional')}
        />
        <StyledPanel>
          <StyledHeader>{t('readReplica.addRR.rrInstance')}</StyledHeader>
          <StyledContent>
            <Box>
              <Box mb={2}>
                <YBCheckboxField
                  label={t('readReplica.addRR.primaryRRInstanceSame')}
                  control={control}
                  name={SAME_AS_PRIMARY_INST_FIELD}
                  size="large"
                  dataTestId="keep-rr-primary-same-field"
                />
              </Box>
              <StyledPanelWrapper>
                {isRuntimeConfigLoading || isProviderRuntimeConfigLoading ? (
                  <Box display="flex" alignItems="center" justifyContent="center" width="100%">
                    <CircularProgress />
                  </Box>
                ) : (
                  <InstanceBox>
                    {provider && (
                      <InstanceTypeField
                        isMaster={false}
                        disabled={!!sameAsPrimary}
                        provider={provider}
                        //pass regions selected in first step
                        // regions={}
                      />
                    )}
                    <VolumeInfoField
                      isMaster={false}
                      maxVolumeCount={maxVolumeCount}
                      disabled={!!sameAsPrimary}
                      provider={provider}
                      //pass regions selected in first step
                      // regions={}
                    />
                  </InstanceBox>
                )}
              </StyledPanelWrapper>
            </Box>
          </StyledContent>
        </StyledPanel>
      </Box>
    </FormProvider>
  );
});
