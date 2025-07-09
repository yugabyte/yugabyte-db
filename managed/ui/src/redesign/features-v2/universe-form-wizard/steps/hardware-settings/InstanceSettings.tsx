import { forwardRef, useContext, useEffect, useImperativeHandle } from 'react';
import { FormProvider, useForm } from 'react-hook-form';
import { mui, YBAccordion, YBCheckboxField } from '@yugabyte-ui-library/core';
import {
  CreateUniverseContext,
  CreateUniverseContextMethods,
  StepsRef
} from '../../CreateUniverseContext';
import { InstanceSettingProps } from './dtos';
import { StyledContent, StyledHeader, StyledPanel } from '../../components/DefaultComponents';
import {
  CPUArchField,
  LinuxVersionField,
  SpotInstanceField,
  InstanceTypeField,
  VolumeInfoField,
  StorageTypeField,
  K8NodeSpecField,
  K8VolumeInfoField
} from '../../fields';
import { Trans, useTranslation } from 'react-i18next';
import { useQuery } from 'react-query';
import {
  CloudType,
  RunTimeConfigEntry
} from '@app/redesign/features/universe/universe-form/utils/dto';
import { api, QUERY_KEY } from '@app/redesign/features/universe/universe-form/utils/api';
import { VM_PATCHING_RUNTIME_CONFIG } from '@app/components/configRedesign/providerRedesign/components/linuxVersionCatalog/LinuxVersionUtils';
import { useSelector } from 'react-redux';
import { ProviderType } from '../general-settings/dtos';
import { canUseSpotInstance } from '@app/redesign/features/universe/universe-form/form/fields/InstanceTypeField/InstanceTypeFieldHelper';
import {
  DEVICE_INFO_FIELD,
  INSTANCE_TYPE_FIELD,
  LINUX_VERSION_FIELD,
  MASTER_DEVICE_INFO_FIELD,
  MASTER_INSTANCE_TYPE_FIELD,
  MASTER_TSERVER_SAME_FIELD
} from '../../fields/FieldNames';
import { Typography } from '@material-ui/core';

const { Box } = mui;

const isImgBundleSupportedByProvider = (provider: ProviderType) =>
  [CloudType.aws, CloudType.azu, CloudType.gcp].includes(provider?.code);

export const InstanceSettings = forwardRef<StepsRef>((_, forwardRef) => {
  const [
    { instanceSettings, generalSettings, nodesAvailabilitySettings },
    { moveToNextPage, moveToPreviousPage, saveInstanceSettings }
  ] = (useContext(CreateUniverseContext) as unknown) as CreateUniverseContextMethods;

  // Fetch customer scope runtime configs
  const currentCustomer = useSelector((state: any) => state.customer.currentCustomer);
  const customerUUID = currentCustomer?.data?.uuid;
  const { data: runtimeConfigs } = useQuery(
    [QUERY_KEY.fetchCustomerRunTimeConfigs, customerUUID],
    () => api.fetchRunTimeConfigs(true, customerUUID)
  );

  const { data: providerRuntimeConfigs } = useQuery(QUERY_KEY.fetchProviderRunTimeConfigs, () =>
    api.fetchRunTimeConfigs(true, provider?.uuid)
  );

  const useK8CustomResourcesObject = runtimeConfigs?.configEntries?.find(
    (c: RunTimeConfigEntry) => c.key === 'yb.use_k8s_custom_resources'
  );
  const useK8CustomResources = !!(useK8CustomResourcesObject?.value === 'true');

  const maxVolumeCount = (runtimeConfigs?.configEntries?.find(
    (c: RunTimeConfigEntry) => c.key === 'yb.max_volume_count'
  )?.value as unknown) as number;

  const osPatchingEnabled =
    runtimeConfigs?.configEntries?.find(
      (c: RunTimeConfigEntry) => c.key === VM_PATCHING_RUNTIME_CONFIG
    )?.value === 'true';

  const { t } = useTranslation('translation', {
    keyPrefix: 'universeForm.instanceConfig'
  });

  const methods = useForm<InstanceSettingProps>({
    defaultValues: instanceSettings
  });
  const { watch, setValue, control } = methods;

  //field data
  const provider = generalSettings?.providerConfiguration;
  const deviceInfo = watch(DEVICE_INFO_FIELD);
  const sameAsTserver = watch(MASTER_TSERVER_SAME_FIELD);
  const instanceType = watch(INSTANCE_TYPE_FIELD);
  const useDedicatedNodes = nodesAvailabilitySettings?.useDedicatedNodes;
  const isK8s = provider?.code === CloudType.kubernetes;

  // Reset Linux version field (ImgBundleUUID) when unsupported provider is selected
  useEffect(() => {
    if (osPatchingEnabled && provider && !isImgBundleSupportedByProvider(provider)) {
      setValue(LINUX_VERSION_FIELD, null);
    }
  }, [provider?.uuid]);

  //Keep master & tserver same
  useEffect(() => {
    if (deviceInfo && sameAsTserver && instanceType) {
      setValue(MASTER_DEVICE_INFO_FIELD, deviceInfo);
      setValue(MASTER_INSTANCE_TYPE_FIELD, instanceType);
    }
  }, [deviceInfo, sameAsTserver, instanceType]);

  const getKubernetesInstanceElement = (isMaster: boolean) => {
    return (
      <Box display="flex" width={'100%'} flexDirection="column">
        <Box>
          <K8NodeSpecField isMaster={isMaster} disabled={isMaster && !!sameAsTserver} />
        </Box>
        <Box mt={2}>
          <K8VolumeInfoField
            isMaster={isMaster}
            disableVolumeSize={false}
            maxVolumeCount={maxVolumeCount}
            disabled={isMaster && !!sameAsTserver}
          />
        </Box>
      </Box>
    );
  };

  const getInstanceMetadataElement = (isMaster: boolean) => {
    return (
      <Box
        sx={{
          display: 'flex',
          width: '100%',
          flexDirection: 'column'
        }}
      >
        <Box
          sx={{
            display: 'flex',
            width: '100%',
            flexDirection: 'column'
          }}
          mt={isMaster ? 0 : 2}
        >
          <InstanceTypeField isMaster={isMaster} disabled={isMaster && !!sameAsTserver} />
        </Box>
        <Box
          sx={{
            display: 'flex',
            width: '100%',
            flexDirection: 'column'
          }}
          mt={2}
        >
          <VolumeInfoField
            isMaster={isMaster}
            maxVolumeCount={maxVolumeCount}
            disabled={isMaster && !!sameAsTserver}
          />
        </Box>
      </Box>
    );
  };

  const getDedicatedContainerElement = (isMaster: boolean) => {
    return (
      <Box
        sx={{
          display: 'flex',
          width: '100%'
        }}
      >
        {getInstanceMetadataElement(isMaster)}
      </Box>
    );
  };

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () => {
        methods.handleSubmit((data) => {
          saveInstanceSettings(data);
          moveToNextPage();
        })();
      },
      onPrev: () => {
        moveToPreviousPage();
      }
    }),
    []
  );

  const showDedicatedNodesSection = !!(useDedicatedNodes || (useK8CustomResources && isK8s));

  return (
    <FormProvider {...methods}>
      <StyledPanel>
        <StyledHeader>
          {showDedicatedNodesSection ? t('tserver') : t('clusterInstance')}
        </StyledHeader>
        <StyledContent>
          <Box
            sx={{
              display: 'flex',
              width: '734px',
              flexDirection: 'column',
              backgroundColor: '#FBFCFD',
              border: '1px solid #D7DEE4',
              borderRadius: '8px',
              padding: '24px'
            }}
          >
            <Box
              sx={{
                width: 480
              }}
            >
              {osPatchingEnabled && provider && isImgBundleSupportedByProvider(provider) && (
                <>
                  <Box>
                    <CPUArchField disabled={false} />
                  </Box>
                  <Box mt={2}>
                    <LinuxVersionField disabled={false} />
                  </Box>
                </>
              )}
              {provider &&
                [CloudType.aws, CloudType.gcp, CloudType.azu].includes(provider?.code) &&
                canUseSpotInstance(providerRuntimeConfigs) && (
                  <Box width="480" display="flex" flexDirection="column" mt={2}>
                    <SpotInstanceField disabled={false} cloudType={provider?.code} />
                  </Box>
                )}

              <Box
                sx={{
                  width: 480
                }}
                flex={1}
                display="flex"
                flexDirection="row"
              >
                {!isK8s && (
                  <>
                    {!useDedicatedNodes
                      ? getInstanceMetadataElement(false)
                      : getDedicatedContainerElement(false)}
                  </>
                )}
                {useK8CustomResources && isK8s && getKubernetesInstanceElement(false)}
                {isK8s && !useK8CustomResources && getInstanceMetadataElement(false)}
              </Box>

              {/* GCP dedicated Universe */}
              {deviceInfo && provider?.code === CloudType.gcp && useDedicatedNodes && (
                <Box
                  sx={{
                    width: 480
                  }}
                  mt={2}
                >
                  <StorageTypeField disabled={false} />
                </Box>
              )}
            </Box>
          </Box>
        </StyledContent>
      </StyledPanel>

      <Box mb={3} />

      {showDedicatedNodesSection && (
        <YBAccordion titleContent={<>{t('master')}</>} sx={{ width: '100%', padding: 1 }}>
          <Box>
            <Box>
              <YBCheckboxField
                label={t('keepMasterTserverSame')}
                control={control}
                name={MASTER_TSERVER_SAME_FIELD}
                size="large"
              />
            </Box>
            <Box
              sx={{
                display: 'flex',
                width: '734px',
                flexDirection: 'column',
                backgroundColor: '#FBFCFD',
                border: '1px solid #D7DEE4',
                borderRadius: '8px',
                padding: '24px'
              }}
              mt={2}
            >
              <Box
                sx={{
                  width: 480
                }}
              >
                <Box flex={1} display="flex" flexDirection="row">
                  {!isK8s && <>{useDedicatedNodes && getDedicatedContainerElement(true)}</>}
                  {useK8CustomResources && isK8s && getKubernetesInstanceElement(true)}
                </Box>
              </Box>
              <Box mt={4} sx={{ width: 480 }}>
                <Typography variant="subtitle1" color="textSecondary">
                  <Trans i18nKey="masterNote">
                    {t('masterNote')}
                    <b />
                  </Trans>
                </Typography>
              </Box>
            </Box>
          </Box>
        </YBAccordion>
      )}
    </FormProvider>
  );
});

InstanceSettings.displayName = 'InstanceSettings';
