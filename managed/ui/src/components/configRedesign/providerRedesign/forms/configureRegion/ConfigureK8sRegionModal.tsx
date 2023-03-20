/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React from 'react';
import clsx from 'clsx';
import { FormHelperText, makeStyles } from '@material-ui/core';
import { FormProvider, SubmitHandler, useForm } from 'react-hook-form';
import { array, object, string } from 'yup';
import { yupResolver } from '@hookform/resolvers/yup';

import { YBModal, YBModalProps } from '../../../../../redesign/components';
import { ProviderCode, VPCSetupType } from '../../constants';
import { YBReactSelectField } from '../../components/YBReactSelect/YBReactSelectField';
import { ConfigureK8sAvailabilityZoneField } from './ConfigureK8sAvailabilityZoneField';
import { K8sCertIssuerType, K8sRegionFieldLabel, RegionOperation } from './constants';
import { getRegionOptions } from './utils';
import { generateLowerCaseAlphanumericId } from '../utils';

interface ConfigureK8sRegionModalProps extends YBModalProps {
  configuredRegions: K8sRegionField[];
  onRegionSubmit: (region: K8sRegionField) => void;
  onClose: () => void;
  providerCode: ProviderCode;
  regionOperation: RegionOperation;

  regionSelection?: K8sRegionField;
  vpcSetupType?: VPCSetupType;
}

interface K8sRegionCloudInfoFormValues {
  certIssuerType: K8sCertIssuerType;

  certIssuerName?: string;
  kubeConfigContent?: File;
  kubeDomain?: string;
  kubeNamespace?: string;
  kubePodAddressTemplate?: string;
  kubernetesStorageClasses?: string;
  overrides?: string;
}

interface K8sAvailabilityZoneFormValues extends K8sRegionCloudInfoFormValues {
  code: string;
}
export interface ConfigureK8sRegionFormValues extends K8sRegionCloudInfoFormValues {
  regionData: { value: { code: string; zoneOptions: string[] }; label: string };
  zones: K8sAvailabilityZoneFormValues[];

  fieldId?: string;
}
export interface K8sRegionField extends ConfigureK8sRegionFormValues {
  fieldId: string;
  code: string;
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
  onRegionSubmit,
  onClose,
  regionOperation,
  providerCode,
  regionSelection,
  vpcSetupType,
  ...modalProps
}: ConfigureK8sRegionModalProps) => {
  const validationSchema = object().shape({
    regionData: object().required(`${K8sRegionFieldLabel.REGION} is required.`),
    zones: array().of(
      object().shape({
        code: string().required('Zone code is required.')
      })
    )
  });
  const formMethods = useForm<ConfigureK8sRegionFormValues>({
    defaultValues: regionSelection,
    resolver: yupResolver(validationSchema)
  });
  const classes = useStyles();

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
      fieldId: formValues.fieldId ?? generateLowerCaseAlphanumericId()
    };
    onRegionSubmit(newRegion);
    formMethods.reset();
    onClose();
  };

  const configuredRegionCodes = configuredRegions.map((configuredRegion) => configuredRegion.code);
  const regionOptions = getRegionOptions(providerCode).filter(
    (regionOption) =>
      regionSelection?.code === regionOption.value.code ||
      !configuredRegionCodes.includes(regionOption.value.code)
  );
  return (
    <FormProvider {...formMethods}>
      <YBModal
        title="Add Region"
        titleIcon={<i className={clsx('fa fa-plus', classes.titleIcon)} />}
        submitLabel="Add Region"
        cancelLabel="Cancel"
        submitTestId="ConfigureK8sRegionModal-SubmitButton"
        cancelTestId="ConfigureK8sRegionModal-CancelButton"
        onSubmit={formMethods.handleSubmit(onSubmit)}
        onClose={onClose}
        {...modalProps}
      >
        <div className={classes.formField}>
          <div>{K8sRegionFieldLabel.REGION}</div>
          <YBReactSelectField
            control={formMethods.control}
            name="regionData"
            options={regionOptions}
          />
        </div>
        <div>
          <ConfigureK8sAvailabilityZoneField
            className={classes.manageAvailabilityZoneField}
            isSubmitting={formMethods.formState.isSubmitting}
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
