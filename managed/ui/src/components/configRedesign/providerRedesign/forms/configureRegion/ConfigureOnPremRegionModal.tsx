/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import clsx from 'clsx';
import { FormHelperText, makeStyles } from '@material-ui/core';
import { FormProvider, SubmitHandler, useForm } from 'react-hook-form';
import { array, number, object, string } from 'yup';
import { yupResolver } from '@hookform/resolvers/yup';

import { YBInputField, YBModal, YBModalProps } from '../../../../../redesign/components';
import { OnPremRegionFieldLabel, RegionOperation } from './constants';
import { ConfigureOnPremAvailabilityZoneField } from './ConfigureOnPremAvailabilityZoneField';
import { generateLowerCaseAlphanumericId, getIsRegionFormDisabled } from '../utils';
import { ACCEPTABLE_CHARS } from '../../../../config/constants';
import {
  ReactSelectOption,
  YBReactSelectField
} from '../../components/YBReactSelect/YBReactSelectField';
import { ON_PREM_CUSTOM_LOCATION, ON_PREM_LOCATIONS } from '../../providerRegionsData';
import { RegionOperationLabel } from '../../constants';

interface ConfigureOnPremRegionModalProps extends YBModalProps {
  configuredRegions: ConfigureOnPremRegionFormValues[];
  onRegionSubmit: (region: ConfigureOnPremRegionFormValues) => void;
  onClose: () => void;
  regionOperation: RegionOperation;
  isProviderFormDisabled: boolean;

  regionSelection?: ConfigureOnPremRegionFormValues;
}

export interface OnPremAvailabilityZoneFormValues {
  code: string;
}

export interface ConfigureOnPremRegionFormValues {
  fieldId: string;
  code: string;
  name: string;
  location: { value: string; label: string };
  longitude: number;
  latitude: number;
  zones: OnPremAvailabilityZoneFormValues[];
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

const ON_PREM_LOCATION_OPTIONS = Object.keys(ON_PREM_LOCATIONS).map((locationName) => ({
  value: locationName,
  label: locationName
}));

export const ConfigureOnPremRegionModal = ({
  configuredRegions,
  isProviderFormDisabled,
  onRegionSubmit,
  onClose,
  regionSelection,
  regionOperation,
  ...modalProps
}: ConfigureOnPremRegionModalProps) => {
  const configuredRegionCodes = configuredRegions.map((configuredRegion) => configuredRegion.code);
  const validationSchema = object().shape({
    code: string()
      .required(`${OnPremRegionFieldLabel.CODE} is required.`)
      .test(
        'is-unique',
        (testMessageParam) =>
          `${testMessageParam.originalValue} has been previously configured. Please edit or delete that configuration first.`,
        (code) =>
          code ? regionSelection?.code === code || !configuredRegionCodes.includes(code) : false
      ),
    location: object().required(`${OnPremRegionFieldLabel.LOCATION} is required.`),
    latitude: number().when('location', {
      is: ON_PREM_CUSTOM_LOCATION,
      then: number().required(`${OnPremRegionFieldLabel.LATITUDE} is required.`)
    }),
    longitude: number().when('location', {
      is: ON_PREM_CUSTOM_LOCATION,
      then: number().required(`${OnPremRegionFieldLabel.LONGITUDE} is required.`)
    }),
    zones: array().of(
      object().shape({
        code: string()
          .required('Zone code is required.')
          .test(
            'no-invalid-chars',
            (testMessageParam) =>
              `${testMessageParam.originalValue} contains invalid characters. Zone code cannot contain any special characters except '-' and '_'.`,
            (code) => (code ? ACCEPTABLE_CHARS.test(code) : false)
          )
      })
    )
  });
  const formMethods = useForm<ConfigureOnPremRegionFormValues>({
    defaultValues: regionSelection,
    resolver: yupResolver(validationSchema)
  });
  const classes = useStyles();

  const onSubmit: SubmitHandler<ConfigureOnPremRegionFormValues> = (formValues) => {
    if (formValues.zones.length <= 0) {
      formMethods.setError('zones', {
        type: 'min',
        message: 'Region configurations must contain at least one zone.'
      });
      return;
    }
    const newRegion = {
      ...formValues,
      name: formValues.code,
      fieldId: formValues.fieldId ?? generateLowerCaseAlphanumericId()
    };
    onRegionSubmit(newRegion);
    formMethods.reset();
    onClose();
  };

  const onLocationChange = (option: ReactSelectOption) => {
    formMethods.setValue('longitude', ON_PREM_LOCATIONS[option.value]?.longitude ?? 0);
    formMethods.setValue('latitude', ON_PREM_LOCATIONS[option.value]?.latitude ?? 0);
  };

  const isFormDisabled = isProviderFormDisabled || getIsRegionFormDisabled(formMethods.formState);
  const location = formMethods.watch('location', regionSelection?.location);
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
          <div>{OnPremRegionFieldLabel.CODE}</div>
          <YBInputField
            control={formMethods.control}
            name="code"
            placeholder="Enter..."
            disabled={isFormDisabled}
            fullWidth
          />
        </div>
        <div className={classes.formField}>
          <div>{OnPremRegionFieldLabel.LOCATION}</div>
          <YBReactSelectField
            control={formMethods.control}
            name="location"
            onChange={onLocationChange}
            options={ON_PREM_LOCATION_OPTIONS}
            isDisabled={isFormDisabled}
          />
        </div>
        {location?.label === ON_PREM_CUSTOM_LOCATION && (
          <>
            <div className={classes.formField}>
              <div>{OnPremRegionFieldLabel.LATITUDE}</div>
              <YBInputField
                control={formMethods.control}
                disabled={isFormDisabled}
                fullWidth
                name="latitude"
                placeholder="Enter..."
                type="number"
              />
            </div>
            <div className={classes.formField}>
              <div>{OnPremRegionFieldLabel.LONGITUDE}</div>
              <YBInputField
                control={formMethods.control}
                disabled={isFormDisabled}
                fullWidth
                name="longitude"
                placeholder="Enter..."
                type="number"
              />
            </div>
          </>
        )}
        <div>
          <ConfigureOnPremAvailabilityZoneField
            className={classes.manageAvailabilityZoneField}
            isFormDisabled={isFormDisabled}
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
