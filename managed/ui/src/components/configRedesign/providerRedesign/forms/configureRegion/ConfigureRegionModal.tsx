/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { useEffect } from 'react';
import clsx from 'clsx';
import { FormHelperText, makeStyles } from '@material-ui/core';
import { FormProvider, SubmitHandler, useForm } from 'react-hook-form';
import { array, object, string } from 'yup';
import { yupResolver } from '@hookform/resolvers/yup';

import {
  ExposedAZProperties,
  ConfigureAvailabilityZoneField
} from './ConfigureAvailabilityZoneField';
import { ProviderCode, VPCSetupType, YBImageType } from '../../constants';
import { RegionOperation } from './constants';
import { YBInputField, YBModal, YBModalProps } from '../../../../../redesign/components';
import { YBReactSelectField } from '../../components/YBReactSelect/YBReactSelectField';
import { getRegionlabel, getRegionOptions, getZoneOptions } from './utils';
import { generateLowerCaseAlphanumericId } from '../utils';

interface ConfigureRegionModalProps extends YBModalProps {
  configuredRegions: CloudVendorRegionField[];
  onRegionSubmit: (region: CloudVendorRegionField) => void;
  onClose: () => void;
  providerCode: ProviderCode;
  regionOperation: RegionOperation;

  ybImageType?: YBImageType;
  regionSelection?: CloudVendorRegionField;
  vpcSetupType?: VPCSetupType;
}

type ZoneCode = { value: string; label: string; isDisabled: boolean };
type Zones = {
  code: ZoneCode;
  subnet: string;
}[];
export interface ConfigureRegionFormValues {
  fieldId: string;
  regionData: { value: { code: string; zoneOptions: string[] }; label: string };
  zones: Zones;
  securityGroupId?: string;
  vnet?: string;
  ybImage?: string;
  sharedSubnet?: string;
}
export type CloudVendorRegionField = Omit<ConfigureRegionFormValues, 'regionData' | 'zones'> & {
  code: string;
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
  onClose,
  onRegionSubmit,
  providerCode,
  regionOperation,
  regionSelection,
  vpcSetupType,
  ybImageType,
  ...modalProps
}: ConfigureRegionModalProps) => {
  const fieldLabel = {
    region: 'Region',
    vnet: providerCode === ProviderCode.AZU ? 'Virtual Network Name' : 'VPC ID',
    securityGroupId:
      providerCode === ProviderCode.AZU ? 'Security Group Name (Optional)' : 'Security Group ID',
    ybImage:
      providerCode === ProviderCode.AWS
        ? 'Custom AMI ID (Optional Override)'
        : providerCode === ProviderCode.AZU
        ? 'Marketplace Image URN/Shared Gallery Image ID (Optional)'
        : 'Custom Machine Image ID (Optional)',
    sharedSubnet: 'Shared Subnet'
  };
  const shouldExposeField: Record<keyof ConfigureRegionFormValues, boolean> = {
    fieldId: false,
    regionData: true,
    vnet: providerCode !== ProviderCode.GCP && vpcSetupType === VPCSetupType.EXISTING,
    securityGroupId: providerCode !== ProviderCode.GCP && vpcSetupType === VPCSetupType.EXISTING,
    ybImage: providerCode !== ProviderCode.AWS || ybImageType === YBImageType.CUSTOM_AMI,
    sharedSubnet: providerCode === ProviderCode.GCP,
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
    sharedSubnet: string().when([], {
      is: () => shouldExposeField.sharedSubnet && providerCode === ProviderCode.GCP,
      then: string().required(`${fieldLabel.sharedSubnet} is required.`)
    }),
    zones: array().when([], {
      is: () => shouldExposeField.zones,
      then: array().of(
        object().shape({
          code: object(),
          subnet: string().required('Zone subnet is required.')
        })
      )
    })
  });
  const formMethods = useForm<ConfigureRegionFormValues>({
    defaultValues: getDefaultFormValue(providerCode, regionSelection),
    resolver: yupResolver(validationSchema)
  });
  const selectedRegion = formMethods.watch('regionData');
  const { setValue } = formMethods;
  const selectedRegionCode = selectedRegion?.value?.code;
  useEffect(() => {
    setValue('zones', []);
  }, [selectedRegionCode, setValue]);

  const classes = useStyles();

  const configuredRegionCodes = configuredRegions.map((configuredRegion) => configuredRegion.code);
  const regionOptions = getRegionOptions(providerCode).filter(
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
    const newRegion =
      regionOperation === RegionOperation.ADD
        ? {
            ...region,
            zones: [] as ExposedAZProperties[],
            code: regionData.value.code,
            fieldId: generateLowerCaseAlphanumericId()
          }
        : { ...region, zones: [], code: regionData.value.code };
    if (shouldExposeField.zones) {
      newRegion.zones = zones.map((zone) => ({
        code: zone.code.value,
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

  return (
    <FormProvider {...formMethods}>
      <YBModal
        title="Add Region"
        titleIcon={<i className={clsx('fa fa-plus', classes.titleIcon)} />}
        submitLabel="Add Region"
        cancelLabel="Cancel"
        onSubmit={formMethods.handleSubmit(onSubmit)}
        onClose={onClose}
        submitTestId="ConfigureRegionModal-SubmitButton"
        cancelTestId="ConfigureRegionModal-CancelButton"
        {...modalProps}
      >
        {shouldExposeField.regionData && (
          <div className={classes.formField}>
            <div>{fieldLabel.region}</div>
            <YBReactSelectField
              control={formMethods.control}
              name="regionData"
              options={regionOptions}
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
              fullWidth
            />
          </div>
        )}
        {shouldExposeField.zones && (
          <div>
            <ConfigureAvailabilityZoneField
              className={classes.manageAvailabilityZoneField}
              zoneCodeOptions={selectedRegion?.value?.zoneOptions}
              isSubmitting={formMethods.formState.isSubmitting}
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
  providerCode: ProviderCode,
  regionSelection: CloudVendorRegionField | undefined
) => {
  if (regionSelection === undefined) {
    return {
      zones: [] as Zones
    };
  }
  const { code: currentRegionCode, zones, ...currentRegion } = regionSelection;
  return {
    ...currentRegion,
    regionData: {
      value: {
        code: currentRegionCode,
        zoneOptions: getZoneOptions(providerCode, currentRegionCode)
      },
      label: getRegionlabel(providerCode, currentRegionCode)
    },
    zones: zones.map((zone) => ({
      code: { value: zone.code, label: zone.code, isDiabled: false },
      subnet: zone.subnet
    }))
  };
};
