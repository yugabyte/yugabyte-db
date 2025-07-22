import { forwardRef, useContext, useEffect, useImperativeHandle } from 'react';
import { FormProvider, useForm } from 'react-hook-form';
import { yupResolver } from '@hookform/resolvers/yup';
import { Trans, useTranslation } from 'react-i18next';
import { mui, YBAccordion, YBCheckboxField } from '@yugabyte-ui-library/core';
import { useRuntimeConfigValues } from '@app/redesign/features-v2/universe-form-wizard/helpers/utils';
import {
  CreateUniverseContext,
  CreateUniverseContextMethods,
  CreateUniverseSteps,
  StepsRef
} from '@app/redesign/features-v2/universe-form-wizard/CreateUniverseContext';
import { InstanceSettingProps } from '@app/redesign/features-v2/universe-form-wizard/steps/hardware-settings/dtos';
import {
  StyledContent,
  StyledHeader,
  StyledPanel
} from '@app/redesign/features-v2/universe-form-wizard/components/DefaultComponents';
import {
  CPUArchField,
  LinuxVersionField,
  SpotInstanceField,
  InstanceTypeField,
  VolumeInfoField,
  StorageTypeField,
  K8NodeSpecField,
  K8VolumeInfoField
} from '@app/redesign/features-v2/universe-form-wizard/fields';
import { ProviderType } from '@app/redesign/features-v2/universe-form-wizard/steps/general-settings/dtos';
import { ResilienceType } from '@app/redesign/features-v2/universe-form-wizard/steps/resilence-regions/dtos';
import {
  DEVICE_INFO_FIELD,
  INSTANCE_TYPE_FIELD,
  LINUX_VERSION_FIELD,
  MASTER_DEVICE_INFO_FIELD,
  MASTER_INSTANCE_TYPE_FIELD,
  MASTER_TSERVER_SAME_FIELD
} from '@app/redesign/features-v2/universe-form-wizard/fields/FieldNames';
import { InstanceSettingsValidationSchema } from '@app/redesign/features-v2/universe-form-wizard/steps/hardware-settings/ValidationSchema';
import { CloudType } from '@app/redesign/features/universe/universe-form/utils/dto';

const { Box, Typography, CircularProgress } = mui;

const isImgBundleSupportedByProvider = (provider: ProviderType) =>
  [CloudType.aws, CloudType.azu, CloudType.gcp].includes(provider?.code);

const InstanceBox = ({ children }: { children: React.ReactNode }) => (
  <Box sx={{ width: 480, display: 'flex', flexDirection: 'column', gap: 2 }}>{children}</Box>
);

const PanelWrapper = ({ children }: { children: React.ReactNode }) => (
  <Box
    sx={(theme) => ({
      display: 'flex',
      width: '734px',
      flexDirection: 'column',
      backgroundColor: '#FBFCFD',
      border: `1px solid ${theme.palette.grey[300]}`,
      borderRadius: '8px',
      padding: '24px'
    })}
  >
    {children}
  </Box>
);

export const InstanceSettings = forwardRef<StepsRef>((_, forwardRef) => {
  const [
    { instanceSettings, generalSettings, nodesAvailabilitySettings, resilienceAndRegionsSettings },
    { moveToNextPage, moveToPreviousPage, saveInstanceSettings, setActiveStep }
  ] = (useContext(CreateUniverseContext) as unknown) as CreateUniverseContextMethods;

  const provider = generalSettings?.providerConfiguration;
  const isK8s = provider?.code === CloudType.kubernetes;

  //Runtime configs
  const {
    osPatchingEnabled,
    useK8CustomResources,
    maxVolumeCount,
    canUseSpotInstance,
    isRuntimeConfigLoading,
    isProviderRuntimeConfigLoading
  } = useRuntimeConfigValues(provider?.uuid);
  //Runtime configs

  const { t } = useTranslation('translation', {
    keyPrefix: 'universeForm.instanceConfig'
  });

  const methods = useForm<InstanceSettingProps>({
    defaultValues: instanceSettings,
    resolver: yupResolver(InstanceSettingsValidationSchema(t))
  });
  const { watch, setValue, control } = methods;

  const deviceInfo = watch(DEVICE_INFO_FIELD);
  const sameAsTserver = watch(MASTER_TSERVER_SAME_FIELD);
  const instanceType = watch(INSTANCE_TYPE_FIELD);
  const useDedicatedNodes = nodesAvailabilitySettings?.useDedicatedNodes;

  useEffect(() => {
    if (osPatchingEnabled && provider && !isImgBundleSupportedByProvider(provider)) {
      setValue(LINUX_VERSION_FIELD, null);
    }
  }, [provider?.uuid]);

  useEffect(() => {
    if (deviceInfo && sameAsTserver && instanceType) {
      setValue(MASTER_DEVICE_INFO_FIELD, deviceInfo);
      setValue(MASTER_INSTANCE_TYPE_FIELD, instanceType);
    }
  }, [deviceInfo, sameAsTserver, instanceType]);

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () =>
        methods.handleSubmit((data) => {
          saveInstanceSettings(data);
          moveToNextPage();
        })(),
      onPrev: () => {
        if (resilienceAndRegionsSettings?.resilienceType === ResilienceType.SINGLE_NODE) {
          setActiveStep(CreateUniverseSteps.RESILIENCE_AND_REGIONS);
        } else {
          moveToPreviousPage();
        }
      }
    }),
    []
  );

  const showDedicatedNodesSection = !!(useDedicatedNodes || (useK8CustomResources && isK8s));

  if (isRuntimeConfigLoading || isProviderRuntimeConfigLoading) {
    return (
      <StyledPanel>
        <StyledHeader>
          {showDedicatedNodesSection ? t('tserver') : t('clusterInstance')}
        </StyledHeader>
        <StyledContent>
          <PanelWrapper>
            <Box display="flex" alignItems="center" justifyContent="center" width="100%">
              <CircularProgress />
            </Box>
          </PanelWrapper>
        </StyledContent>
      </StyledPanel>
    );
  }

  return (
    <FormProvider {...methods}>
      <StyledPanel>
        <StyledHeader>
          {showDedicatedNodesSection ? t('tserver') : t('clusterInstance')}
        </StyledHeader>
        <StyledContent>
          <PanelWrapper>
            <InstanceBox>
              {osPatchingEnabled && provider && isImgBundleSupportedByProvider(provider) && (
                <>
                  <CPUArchField disabled={false} />
                  <LinuxVersionField disabled={false} />
                </>
              )}
              {provider &&
                [CloudType.aws, CloudType.gcp, CloudType.azu].includes(provider.code) &&
                canUseSpotInstance && (
                  <SpotInstanceField disabled={false} cloudType={provider.code} />
                )}
              {!isK8s &&
                (!useDedicatedNodes ? (
                  <>
                    <InstanceTypeField isMaster={false} disabled={false} />
                    <VolumeInfoField
                      isMaster={false}
                      maxVolumeCount={maxVolumeCount}
                      disabled={false}
                    />
                  </>
                ) : (
                  <>
                    <InstanceTypeField isMaster={false} disabled={false} />
                    <VolumeInfoField
                      isMaster={false}
                      maxVolumeCount={maxVolumeCount}
                      disabled={false}
                    />
                  </>
                ))}
              {isK8s &&
                (useK8CustomResources ? (
                  <>
                    <K8NodeSpecField isMaster={false} disabled={false} />
                    <K8VolumeInfoField
                      isMaster={false}
                      maxVolumeCount={maxVolumeCount}
                      disableVolumeSize={false}
                      disabled={false}
                    />
                  </>
                ) : (
                  <>
                    <InstanceTypeField isMaster={false} disabled={false} />
                    <VolumeInfoField
                      isMaster={false}
                      maxVolumeCount={maxVolumeCount}
                      disabled={false}
                    />
                  </>
                ))}
              {deviceInfo && provider?.code === CloudType.gcp && useDedicatedNodes && (
                <StorageTypeField disabled={false} />
              )}
            </InstanceBox>
          </PanelWrapper>
        </StyledContent>
      </StyledPanel>

      <Box mb={3} />

      {showDedicatedNodesSection && (
        <YBAccordion titleContent={<>{t('master')}</>} sx={{ width: '100%', padding: 1 }}>
          <Box>
            <YBCheckboxField
              label={t('keepMasterTserverSame')}
              control={control}
              name={MASTER_TSERVER_SAME_FIELD}
              size="large"
              dataTestId="keep-master-tserver-same-field"
            />
            <PanelWrapper>
              <InstanceBox>
                {!isK8s && useDedicatedNodes && (
                  <>
                    <InstanceTypeField isMaster={true} disabled={!!sameAsTserver} />
                    <VolumeInfoField
                      isMaster={true}
                      maxVolumeCount={maxVolumeCount}
                      disabled={!!sameAsTserver}
                    />
                  </>
                )}
                {isK8s && useK8CustomResources && (
                  <>
                    <K8NodeSpecField isMaster={true} disabled={!!sameAsTserver} />
                    <K8VolumeInfoField
                      isMaster={true}
                      disableVolumeSize={false}
                      maxVolumeCount={maxVolumeCount}
                      disabled={!!sameAsTserver}
                    />
                  </>
                )}
              </InstanceBox>
              <Box mt={4} sx={{ width: 480 }}>
                <Typography variant="subtitle1" color="textSecondary">
                  <Trans i18nKey="masterNote">
                    {t('masterNote')}
                    <b />
                  </Trans>
                </Typography>
              </Box>
            </PanelWrapper>
          </Box>
        </YBAccordion>
      )}
    </FormProvider>
  );
});

InstanceSettings.displayName = 'InstanceSettings';
