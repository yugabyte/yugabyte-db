import { forwardRef, useContext, useImperativeHandle } from 'react';
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

const { Box, styled } = mui;

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
  const [{ instanceSettings, universeData }, { moveToNextPage, moveToPreviousPage }] = (useContext(
    AddRRContext
  ) as unknown) as AddRRContextMethods;

  const primaryCluster = getClusterByType(universeData!, ClusterSpecClusterType.PRIMARY);

  const provider = {
    uuid: primaryCluster?.provider_spec.provider ?? '',
    code: primaryCluster?.placement_spec?.cloud_list[0].code
  };

  const isK8s = provider.code === CloudType.kubernetes;
  const universeUUID = universeData?.info?.universe_uuid ?? '';
  const providerSpec = primaryCluster?.provider_spec.provider;

  const {
    osPatchingEnabled,
    useK8CustomResources,
    maxVolumeCount,
    canUseSpotInstance,
    isRuntimeConfigLoading,
    isProviderRuntimeConfigLoading
  } = useRuntimeConfigValues(provider.uuid);

  const { t } = useTranslation('translation');

  // , { keyPrefix: 'readReplica.addRR' }
  const methods = useForm<RRInstanceSettingsProps>({
    // resolver: yupResolver(GeneralSettingsValidationSchema(t)),
    defaultValues: instanceSettings,
    mode: 'onChange'
  });

  const { control, watch } = methods;

  const sameAsPrimary = watch(SAME_AS_PRIMARY_INST_FIELD);

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () => {
        return methods.handleSubmit((data) => {
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
        <RRBreadCrumbs groupTitle={t('placement')} subTitle={t('instanceOptional')} />
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
                {/* <InstanceBox>
                  {provider?.code && (
                    <InstanceTypeField
                      isMaster={false}
                      disabled={!!sameAsPrimary}
                      provider={provider}
                      regions={regionnsData} //pass regions selected in first step
                    />
                  )}
                  <VolumeInfoField
                    isMaster={false}
                    maxVolumeCount={maxVolumeCount}
                    disabled={!!sameAsPrimary}
                  />
                </InstanceBox> */}
              </StyledPanelWrapper>
            </Box>
          </StyledContent>
        </StyledPanel>
      </Box>
    </FormProvider>
  );
});
