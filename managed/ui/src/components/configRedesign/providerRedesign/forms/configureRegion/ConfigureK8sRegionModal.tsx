/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import clsx from 'clsx';
import { FormHelperText, makeStyles } from '@material-ui/core';
import { FormProvider, SubmitHandler, useForm } from 'react-hook-form';
import { array, boolean, object, string } from 'yup';
import { yupResolver } from '@hookform/resolvers/yup';

import { YBModal, YBModalProps } from '../../../../../redesign/components';
import {
  KubernetesProvider,
  ProviderCode,
  RegionOperationLabel,
  VPCSetupType
} from '../../constants';
import { YBReactSelectField } from '../../components/YBReactSelect/YBReactSelectField';
import { ConfigureK8sAvailabilityZoneField } from './ConfigureK8sAvailabilityZoneField';
import { K8sCertIssuerType, K8sRegionFieldLabel, RegionOperation } from './constants';
import { getRegionOptions } from './utils';
import { generateLowerCaseAlphanumericId, getIsRegionFormDisabled } from '../utils';
import { useQuery } from 'react-query';
import { api, regionMetadataQueryKey } from '../../../../../redesign/helpers/api';
import { YBErrorIndicator, YBLoading } from '../../../../common/indicators';

interface ConfigureK8sRegionModalProps extends YBModalProps {
  configuredRegions: K8sRegionField[];
  kubernetesProvider: KubernetesProvider;
  onRegionSubmit: (region: K8sRegionField) => void;
  onClose: () => void;
  regionOperation: RegionOperation;
  isProviderFormDisabled: boolean;

  inUseZones?: Set<string>;
  regionSelection?: K8sRegionField;
  vpcSetupType?: VPCSetupType;
}

interface K8sRegionCloudInfoFormValues {
  certIssuerType: K8sCertIssuerType;

  certIssuerName?: string;
  editKubeConfigContent?: boolean;
  kubeConfigFilepath?: string;
  kubeConfigContent?: File;
  kubeDomain?: string;
  kubeNamespace?: string;
  kubePodAddressTemplate?: string;
  kubernetesStorageClass?: string;
  overrides?: string;
}

export interface K8sAvailabilityZoneFormValues extends K8sRegionCloudInfoFormValues {
  code: string;

  editKubeConfigContent?: boolean;
  isNewZone?: boolean;
}
interface ConfigureK8sRegionFormValues {
  regionData: { value: { code: string; zoneOptions: string[] }; label: string };
  zones: K8sAvailabilityZoneFormValues[];

  fieldId?: string;
}
export interface K8sRegionField extends ConfigureK8sRegionFormValues {
  fieldId: string;
  code: string;
  name: string;
}

const useStyles = makeStyles((theme) => ({
  titleIcon: {
    color: theme.palette.orange[500]
  },
  formField: {
    marginTop: theme.spacing(1),
    '&:first-child': {
      marginTop: 0
    }
  },
  manageAvailabilityZoneField: {
    marginTop: theme.spacing(1)
  }
}));

export const ConfigureK8sRegionModal = ({
  configuredRegions,
  isProviderFormDisabled,
  inUseZones = new Set<string>(),
  kubernetesProvider,
  onRegionSubmit,
  onClose,
  regionOperation,
  regionSelection,
  vpcSetupType,
  ...modalProps
}: ConfigureK8sRegionModalProps) => {
  const classes = useStyles();
  const regionMetadataQuery = useQuery(
    regionMetadataQueryKey.detail(ProviderCode.KUBERNETES, kubernetesProvider),
    () => api.fetchRegionMetadata(ProviderCode.KUBERNETES, kubernetesProvider),
    { refetchOnMount: false, refetchOnWindowFocus: false }
  );
  const validationSchema = object().shape({
    regionData: object().required(`${K8sRegionFieldLabel.REGION} is required.`),
    zones: array().of(
      object().shape({
        code: string().required('Zone code is required.'),
        editKubeConfigContent: boolean(),
        isNewZone: boolean()
      })
    )
  });
  const formMethods = useForm<ConfigureK8sRegionFormValues>({
    defaultValues: regionSelection,
    resolver: yupResolver(validationSchema)
  });

  const onSubmit: SubmitHandler<ConfigureK8sRegionFormValues> = async (formValues) => {
    if (formValues.zones.length <= 0) {
      formMethods.setError('zones', {
        type: 'min',
        message: 'Region configurations must contain at least one zone.'
      });
      return;
    }
    const newRegion = {
      ...formValues,
      code: formValues.regionData.value.code,
      name: formValues.regionData.label,
      fieldId: formValues.fieldId ?? generateLowerCaseAlphanumericId(),
      zones: formValues.zones.map((zone) => {
        const { isNewZone, ...zoneValues } = zone;
        // `isNewZone` should be kept internal since this is only used
        // to track whether we should show an `editKubeConfigContent` toggle in the
        // component for configuring k8s AZs
        return { ...zoneValues };
      })
    };
    onRegionSubmit(newRegion);
    formMethods.reset();
    onClose();
  };

  if (regionMetadataQuery.isLoading || regionMetadataQuery.isIdle) {
    return <YBLoading />;
  }
  if (regionMetadataQuery.isError) {
    return <YBErrorIndicator customErrorMessage="Error fetching region metadata." />;
  }

  const configuredRegionCodes = configuredRegions.map((configuredRegion) => configuredRegion.code);
  const regionOptions = getRegionOptions(regionMetadataQuery.data)
    .filter(
      (regionOption) =>
        regionSelection?.code === regionOption.value.code ||
        !configuredRegionCodes.includes(regionOption.value.code)
    )
    .sort((regionOptionA, regionOptionB) => (regionOptionA.label > regionOptionB.label ? 1 : -1));
  const isFormDisabled = isProviderFormDisabled || getIsRegionFormDisabled(formMethods.formState);
  const isRegionInUse = inUseZones.size > 0;
  const isRegionFieldDisabled = isFormDisabled || isRegionInUse;
  return (
    <FormProvider {...formMethods}>
      <YBModal
        title={`${RegionOperationLabel[regionOperation]} Region`}
        titleIcon={<i className={clsx('fa fa-plus', classes.titleIcon)} />}
        submitLabel={
          regionOperation !== RegionOperation.VIEW
            ? `${RegionOperationLabel[regionOperation]} Region`
            : undefined
        }
        cancelLabel="Cancel"
        onSubmit={formMethods.handleSubmit(onSubmit)}
        onClose={onClose}
        submitTestId="ConfigureRegionModal-SubmitButton"
        cancelTestId="ConfigureRegionModal-CancelButton"
        buttonProps={{
          primary: { disabled: isFormDisabled }
        }}
        {...modalProps}
      >
        <div className={classes.formField}>
          <div>{K8sRegionFieldLabel.REGION}</div>
          <YBReactSelectField
            control={formMethods.control}
            name="regionData"
            options={regionOptions}
            isDisabled={isRegionFieldDisabled}
          />
        </div>
        <div>
          <ConfigureK8sAvailabilityZoneField
            className={classes.manageAvailabilityZoneField}
            regionOperation={regionOperation}
            isFormDisabled={isFormDisabled}
            inUseZones={inUseZones}
          />
          {formMethods.formState.errors.zones?.message && (
            <FormHelperText error={true}>
              {formMethods.formState.errors.zones?.message}
            </FormHelperText>
          )}
        </div>
      </YBModal>
    </FormProvider>
  );
};
