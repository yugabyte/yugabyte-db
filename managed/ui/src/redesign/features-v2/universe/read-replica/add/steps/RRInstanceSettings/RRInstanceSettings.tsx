import { forwardRef, useContext, useEffect, useImperativeHandle, useMemo } from 'react';
import { useForm, FormProvider } from 'react-hook-form';
import { yupResolver } from '@hookform/resolvers/yup';
import { useTranslation } from 'react-i18next';
import { mui, YBCheckboxField } from '@yugabyte-ui-library/core';
import { RRBreadCrumbs } from '../../ReadReplicaBreadCrumbs';
import {
  StepsRef,
  AddRRContext,
  AddRRContextMethods,
  AddReadReplicaSteps
} from '../../AddReadReplicaContext';
import { InstanceSettingProps } from '@app/redesign/features-v2/universe/create-universe/steps/hardware-settings/dtos';
import {
  StyledPanel,
  StyledHeader,
  StyledContent
} from '@app/redesign/features-v2/universe/create-universe/components/DefaultComponents';
import { TotalNodesBadge } from '@app/redesign/features-v2/universe/create-universe/components/TotalNodesBadge';
import { InstanceBox } from '@app/redesign/features-v2/universe/create-universe/steps';
import {
  InstanceTypeField,
  VolumeInfoField,
  K8NodeSpecField,
  K8VolumeInfoField,
  EBSVolumeField,
  EBSKmsConfigField
} from '@app/redesign/features-v2/universe/create-universe/fields';
import { ENABLE_EBS_CONFIG_FIELD } from '@app/redesign/features-v2/universe/create-universe/fields/FieldNames';
import { RRInstanceSettingsValidationSchema } from './ValidationSchema';
import { useRuntimeConfigValues } from '@app/redesign/features-v2/universe/create-universe/helpers/utils';
import { getClusterByType } from '@app/redesign/features-v2/universe/edit-universe/EditUniverseUtils';
import { ClusterSpecClusterType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { CloudType } from '@app/redesign/features/universe/universe-form/utils/dto';
import { ProviderType } from '@app/redesign/features-v2/universe/create-universe/steps/general-settings/dtos';
import { Region } from '@app/redesign/features/universe/universe-form/utils/dto';
import { buildRRInstanceSettingsFromCluster } from '../../../readReplicaUtils';
import { sumReadReplicaNodeCounts } from '../../addReadReplicaClusterPayload';

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
    { instanceSettings, universeData, regionsAndAZ },
    { moveToNextPage, moveToPreviousPage, saveInstanceSettings, setActiveStep }
  ] = useContext(AddRRContext) as unknown as AddRRContextMethods;

  const primaryCluster = getClusterByType(universeData!, ClusterSpecClusterType.PRIMARY);
  const useDedicatedNodes = Boolean(primaryCluster?.node_spec?.dedicated_nodes);
  const totalNodes = regionsAndAZ ? sumReadReplicaNodeCounts(regionsAndAZ) : 0;
  const goToPlacementRegions = () => setActiveStep(AddReadReplicaSteps.REGIONS_AND_AZ);

  const provider: Partial<ProviderType> = {
    uuid: primaryCluster?.provider_spec.provider ?? '',
    code: (primaryCluster?.placement_spec?.cloud_list[0].code ?? '') as CloudType
  };

  const {
    maxVolumeCount,
    isRuntimeConfigLoading,
    isProviderRuntimeConfigLoading,
    ebsVolumeEnabled,
    useK8CustomResources
  } = useRuntimeConfigValues(provider.uuid);

  const { t } = useTranslation('translation', { keyPrefix: 'readReplica.addRR' });

  const methods = useForm<RRInstanceSettingsProps>({
    defaultValues: instanceSettings,
    mode: 'onChange',
    resolver: yupResolver(
      RRInstanceSettingsValidationSchema(t, useK8CustomResources, provider?.code)
    )
  });

  const { control, watch, reset, getValues } = methods;

  const regionsForHardwareFields = useMemo((): Region[] | undefined => {
    const formRegions = regionsAndAZ?.regions;
    if (!formRegions?.length) return undefined;
    const uuids = [
      ...new Set(
        formRegions.map((r) => r.regionUuid).filter((uuid): uuid is string => Boolean(uuid))
      )
    ];
    if (!uuids.length) return undefined;
    return uuids.map((uuid) => ({ uuid }) as Region);
  }, [regionsAndAZ]);

  const isK8s = provider?.code === CloudType.kubernetes;
  const sameAsPrimary = watch(SAME_AS_PRIMARY_INST_FIELD);
  const ebsEnabled = watch(ENABLE_EBS_CONFIG_FIELD);

  // Reset form to initial values from primary cluster when sameAsPrimary is true
  useEffect(() => {
    if (sameAsPrimary && primaryCluster && universeData) {
      const initialInstanceSettings: RRInstanceSettingsProps = buildRRInstanceSettingsFromCluster(
        primaryCluster,
        universeData!.info!.arch,
        true
      );
      const currentInstanceType = getValues('instanceType');
      // On k8s, primary cluster may not include node_spec.instance_type (custom resources path).
      // Preserve the currently selected value so toggling "same as primary" does not blank the field.
      const resolvedInstanceType =
        provider?.code === CloudType.kubernetes && !initialInstanceSettings.instanceType
          ? (currentInstanceType ?? null)
          : initialInstanceSettings.instanceType;
      reset({ ...initialInstanceSettings, instanceType: resolvedInstanceType });
    }
  }, [sameAsPrimary, primaryCluster, universeData, reset, getValues, provider?.code]);

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
        <RRBreadCrumbs groupTitle={t('hardware')} subTitle={t('instanceOptional')} />
        <StyledPanel>
          <Box>
            <StyledHeader>{t('rrInstance')}</StyledHeader>
            <Box sx={{ px: '24px', pb: '16px' }}>
              <TotalNodesBadge
                label={useDedicatedNodes ? t('totalTServerNodes') : t('totalNodes')}
                count={totalNodes}
                onEdit={goToPlacementRegions}
                dataTestId="rr-instance-settings-total-nodes"
              />
            </Box>
          </Box>
          <StyledContent>
            <Box>
              <Box mb={2}>
                <YBCheckboxField
                  label={t('primaryRRInstanceSame')}
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
                    {provider &&
                      (isK8s && useK8CustomResources ? (
                        <>
                          <K8NodeSpecField
                            isMaster={false}
                            disabled={!!sameAsPrimary}
                            provider={provider}
                          />
                          <K8VolumeInfoField
                            isMaster={false}
                            maxVolumeCount={maxVolumeCount}
                            disableVolumeSize={false}
                            disabled={!!sameAsPrimary}
                            provider={provider}
                          />
                        </>
                      ) : (
                        <>
                          <InstanceTypeField
                            isMaster={false}
                            disabled={!!sameAsPrimary}
                            provider={provider}
                            regions={regionsForHardwareFields}
                          />
                          <VolumeInfoField
                            isMaster={false}
                            maxVolumeCount={maxVolumeCount}
                            disabled={!!sameAsPrimary}
                            provider={provider}
                            regions={regionsForHardwareFields}
                          />
                        </>
                      ))}
                    {ebsVolumeEnabled && provider?.code === CloudType.aws && (
                      <EBSVolumeField disabled={!!sameAsPrimary} />
                    )}
                    {ebsVolumeEnabled && provider?.code === CloudType.aws && ebsEnabled && (
                      <EBSKmsConfigField disabled={!!sameAsPrimary} />
                    )}
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

RRInstanceSettings.displayName = 'RRInstanceSettings';
