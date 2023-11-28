/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React from 'react';
import clsx from 'clsx';
import { FormHelperText, makeStyles } from '@material-ui/core';
import { FormProvider, SubmitHandler, useForm } from 'react-hook-form';
import { array, object, string } from 'yup';
import { useQuery } from 'react-query';
import { yupResolver } from '@hookform/resolvers/yup';

import {
  ExposedAZProperties,
  ConfigureAvailabilityZoneField
} from './ConfigureAvailabilityZoneField';
import {
  CloudVendorProviders,
  ProviderCode,
  RegionOperationLabel,
  VPCSetupType,
  YBImageType
} from '../../constants';
import { RegionOperation } from './constants';
import { YBInputField, YBModal, YBModalProps } from '../../../../../redesign/components';
import {
  ReactSelectOption,
  YBReactSelectField
} from '../../components/YBReactSelect/YBReactSelectField';
import { getRegionOption, getRegionOptions } from './utils';
import { generateLowerCaseAlphanumericId, getIsRegionFormDisabled } from '../utils';
import { api, regionMetadataQueryKey } from '../../../../../redesign/helpers/api';
import { YBErrorIndicator, YBLoading } from '../../../../common/indicators';
import { RegionMetadataResponse } from '../../types';

interface ConfigureRegionModalProps extends YBModalProps {
  configuredRegions: CloudVendorRegionField[];
  onRegionSubmit: (region: CloudVendorRegionField) => void;
  onClose: () => void;
  providerCode: typeof CloudVendorProviders[number];
  regionOperation: RegionOperation;
  isEditProvider: boolean;
  isProviderFormDisabled: boolean;

  inUseZones?: Set<string>;
  ybImageType?: YBImageType;
  regionSelection?: CloudVendorRegionField;
  vpcSetupType?: VPCSetupType;
}

type ZoneCode = { value: string; label: string; isDisabled: boolean };
type Zones = {
  code: ZoneCode | undefined;
  subnet: string;
}[];
export interface ConfigureRegionFormValues {
  fieldId: string;
  regionData: { value: { code: string; zoneOptions: string[] }; label: string };
  zones: Zones;

  instanceTemplate?: string;
  securityGroupId?: string;
  sharedSubnet?: string;
  vnet?: string;
  ybImage?: string;
}
export type CloudVendorRegionField = Omit<ConfigureRegionFormValues, 'regionData' | 'zones'> & {
  code: string;
  name: string;
  zones: ExposedAZProperties[];
};

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

export const ConfigureRegionModal = ({
  configuredRegions,
  isEditProvider,
  isProviderFormDisabled,
  inUseZones = new Set<string>(),
  onClose,
  onRegionSubmit,
  providerCode,
  regionOperation,
  regionSelection,
  vpcSetupType,
  ybImageType,
  ...modalProps
}: ConfigureRegionModalProps) => {
  const regionMetadataQuery = useQuery(
    regionMetadataQueryKey.detail(providerCode),
    () => api.fetchRegionMetadata(providerCode),
    { refetchOnMount: false, refetchOnWindowFocus: false }
  );
  const classes = useStyles();

  const fieldLabel = {
    region: 'Region',
    vnet: providerCode === ProviderCode.AZU ? 'Virtual Network Name' : 'VPC ID',
    securityGroupId:
      providerCode === ProviderCode.AZU ? 'Security Group Name (Optional)' : 'Security Group ID',
    ybImage:
      providerCode === ProviderCode.AWS
        ? 'AMI ID'
        : providerCode === ProviderCode.AZU
        ? 'Marketplace Image URN/Shared Gallery Image ID (Optional)'
        : 'Custom Machine Image ID (Optional)',
    sharedSubnet: 'Shared Subnet',
    instanceTemplate: 'Instance Template (Optional)'
  };
  const shouldExposeField: Record<keyof ConfigureRegionFormValues, boolean> = {
    fieldId: false,
    instanceTemplate: providerCode === ProviderCode.GCP,
    regionData: true,
    securityGroupId: providerCode !== ProviderCode.GCP && vpcSetupType === VPCSetupType.EXISTING,
    sharedSubnet: providerCode === ProviderCode.GCP,
    vnet: providerCode !== ProviderCode.GCP && vpcSetupType === VPCSetupType.EXISTING,
    ybImage: providerCode !== ProviderCode.AWS || ybImageType === YBImageType.CUSTOM_AMI,
    zones: providerCode !== ProviderCode.GCP
  };
  const validationSchema = object().shape({
    regionData: object().required(`${fieldLabel.region} is required.`),
    vnet: string().when([], {
      is: () => shouldExposeField.vnet,
      then: string().required(`${fieldLabel.vnet} is required.`)
    }),
    securityGroupId: string().when([], {
      is: () => shouldExposeField.securityGroupId && providerCode === ProviderCode.AWS,
      then: string().required(`${fieldLabel.securityGroupId} is required.`)
    }),
    ybImage: string().when([], {
      is: () =>
        shouldExposeField.ybImage && ybImageType === YBImageType.CUSTOM_AMI && !isEditProvider,
      then: string().required(`${fieldLabel.ybImage} is required.`)
    }),
    sharedSubnet: string().when([], {
      is: () => shouldExposeField.sharedSubnet && providerCode === ProviderCode.GCP,
      then: string().required(`${fieldLabel.sharedSubnet} is required.`)
    }),
    zones: array().when([], {
      is: () => shouldExposeField.zones,
      then: array().of(
        object().shape({
          code: object().required('Zone code is required.'),
          subnet: string().required('Zone subnet is required.')
        })
      )
    })
  });
  const formMethods = useForm<ConfigureRegionFormValues>({
    defaultValues: regionMetadataQuery.data
      ? getDefaultFormValue(regionSelection, regionMetadataQuery.data)
      : {},
    resolver: yupResolver(validationSchema)
  });

  if (regionMetadataQuery.isLoading || regionMetadataQuery.isIdle) {
    return <YBLoading />;
  }
  if (regionMetadataQuery.isError) {
    return <YBErrorIndicator customErrorMessage="Error fetching region metadata." />;
  }
  if (
    formMethods.formState.defaultValues &&
    Object.keys(formMethods.formState.defaultValues).length === 0
  ) {
    // react-hook-form caches the defaultValues on first render.
    // We need to update the defaultValues with reset() after regionMetadataQuery is successful.
    formMethods.reset(getDefaultFormValue(regionSelection, regionMetadataQuery.data));
  }

  const configuredRegionCodes = configuredRegions.map((configuredRegion) => configuredRegion.code);
  const regionOptions = getRegionOptions(regionMetadataQuery.data).filter(
    (regionOption) =>
      regionSelection?.code === regionOption.value.code ||
      !configuredRegionCodes.includes(regionOption.value.code)
  );

  const onSubmit: SubmitHandler<ConfigureRegionFormValues> = (formValues) => {
    if (shouldExposeField.zones && formValues.zones.length <= 0) {
      formMethods.setError('zones', {
        type: 'min',
        message: 'Region configurations must contain at least one zone.'
      });
      return;
    }
    const { regionData, zones, ...region } = formValues;
    const newRegion = {
      ...region,
      code: regionData.value.code,
      name: regionData.label,
      zones: [] as ExposedAZProperties[],
      ...(regionOperation === RegionOperation.ADD && { fieldId: generateLowerCaseAlphanumericId() })
    };

    if (shouldExposeField.zones) {
      newRegion.zones = zones.map((zone) => ({
        code: zone.code?.value ?? '',
        subnet: zone.subnet
      }));
    } else if (providerCode === ProviderCode.GCP) {
      newRegion.zones = regionData.value.zoneOptions.map((zoneOption) => ({
        code: zoneOption,
        subnet: ''
      }));
    }
    onRegionSubmit(newRegion);
    formMethods.reset();
    onClose();
  };

  const selectedRegion = formMethods.watch('regionData');
  const { setValue } = formMethods;
  const currentRegionCode = selectedRegion?.value?.code ?? regionSelection?.code;
  const onRegionChange = (data: ReactSelectOption) => {
    if (data.value.code !== currentRegionCode) {
      setValue('zones', []);
    }
  };

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
        {shouldExposeField.regionData && (
          <div className={classes.formField}>
            <div>{fieldLabel.region}</div>
            <YBReactSelectField
              control={formMethods.control}
              name="regionData"
              options={regionOptions}
              onChange={onRegionChange}
              isDisabled={isRegionFieldDisabled}
            />
          </div>
        )}
        {shouldExposeField.vnet && (
          <div className={classes.formField}>
            <div>{fieldLabel.vnet}</div>
            <YBInputField
              control={formMethods.control}
              name="vnet"
              placeholder="Enter..."
              disabled={isRegionFieldDisabled}
              fullWidth
            />
          </div>
        )}
        {shouldExposeField.securityGroupId && (
          <div className={classes.formField}>
            <div>{fieldLabel.securityGroupId}</div>
            <YBInputField
              control={formMethods.control}
              name="securityGroupId"
              placeholder="Enter..."
              disabled={isRegionFieldDisabled}
              fullWidth
            />
          </div>
        )}
        {shouldExposeField.ybImage && (
          <div className={classes.formField}>
            <div>{fieldLabel.ybImage}</div>
            <YBInputField
              control={formMethods.control}
              name="ybImage"
              placeholder="Enter..."
              disabled={
                isFormDisabled ||
                isRegionFieldDisabled ||
                (providerCode === ProviderCode.AWS &&
                  regionOperation === RegionOperation.EDIT_EXISTING)
              }
              fullWidth
            />
          </div>
        )}
        {shouldExposeField.sharedSubnet && (
          <div className={classes.formField}>
            <div>{fieldLabel.sharedSubnet}</div>
            <YBInputField
              control={formMethods.control}
              name="sharedSubnet"
              placeholder="Enter..."
              disabled={isRegionFieldDisabled}
              fullWidth
            />
          </div>
        )}
        {shouldExposeField.instanceTemplate && (
          <div className={classes.formField}>
            <div>{fieldLabel.instanceTemplate}</div>
            <YBInputField
              control={formMethods.control}
              name="instanceTemplate"
              placeholder="Enter..."
              disabled={isRegionFieldDisabled}
              fullWidth
            />
          </div>
        )}
        {shouldExposeField.zones && (
          <div>
            <ConfigureAvailabilityZoneField
              className={classes.manageAvailabilityZoneField}
              zoneCodeOptions={selectedRegion?.value?.zoneOptions}
              isFormDisabled={isFormDisabled}
              inUseZones={inUseZones}
            />
            {formMethods.formState.errors.zones?.message && (
              <FormHelperText error={true}>
                {formMethods.formState.errors.zones?.message}
              </FormHelperText>
            )}
          </div>
        )}
      </YBModal>
    </FormProvider>
  );
};

const getDefaultFormValue = (
  regionSelection: CloudVendorRegionField | undefined,
  regionMetadataResponse: RegionMetadataResponse
): Partial<ConfigureRegionFormValues> => {
  if (regionSelection === undefined) {
    return {
      zones: [] as Zones
    };
  }
  const { name, code, zones, ...currentRegion } = regionSelection;
  return {
    ...currentRegion,
    regionData: getRegionOption(regionSelection.code, regionMetadataResponse),
    zones: zones.map((zone) => ({
      code: { value: zone.code, label: zone.code, isDisabled: false },
      subnet: zone.subnet
    }))
  };
};
