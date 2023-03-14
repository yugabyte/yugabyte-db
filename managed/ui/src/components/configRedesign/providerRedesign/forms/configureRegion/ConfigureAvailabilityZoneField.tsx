/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import React from 'react';
import Select, { Styles, ValueType } from 'react-select';
import clsx from 'clsx';
import { makeStyles, useTheme } from '@material-ui/core';

import { YBButton } from '../../../../common/forms/fields';
import { YBInput } from '../../../../../redesign/components';

import { CloudVendorAvailabilityZoneMutation } from '../../types';

interface ConfigureAvailabilityZoneFieldProps {
  isSubmitting: boolean;
  selectedZones: ExposedAZProperties[];
  setSelectedZones: (zones: ExposedAZProperties[]) => void;
  zoneCodeOptions: string[] | undefined;
  className?: string;
}

export type ExposedAZProperties = Pick<CloudVendorAvailabilityZoneMutation, 'code' | 'subnet'>;

const useStyles = makeStyles((theme) => ({
  zonesContainer: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(1),

    marginTop: theme.spacing(1)
  },
  zoneConfigContainer: {
    display: 'flex',
    gap: theme.spacing(1)
  },
  removeZoneButton: {
    backgroundColor: 'red',
    padding: theme.spacing(1, 2, 1, 2),
    '& i': {
      color: 'white'
    }
  }
}));

export const ConfigureAvailabilityZoneField = ({
  isSubmitting,
  selectedZones,
  setSelectedZones,
  zoneCodeOptions,
  className
}: ConfigureAvailabilityZoneFieldProps) => {
  const classes = useStyles();
  const theme = useTheme();

  const zoneSelectStyles: Partial<Styles> = {
    container: (provided: any) => ({
      ...provided,
      width: 250,
      minWidth: 250
    }),
    control: (provided: any) => ({
      ...provided,
      height: 42,
      borderRadius: 8,
      border: `1px solid ${theme.palette.ybacolors.ybGray}`
    })
  };

  const addZone = () => {
    setSelectedZones([...selectedZones, { code: '', subnet: '' }]);
  };
  const handleZoneCodeChange = (
    option: ValueType<{ value: string; label: string; isDisabled?: boolean }>,
    zoneIndex: number
  ) => {
    const selectedZonesCopy = Array.from(selectedZones);

    // The below cast is needed to resolve a Typescript error when accessing 'value'.
    // Context on the workaround: https://github.com/JedWatson/react-select/issues/2902
    selectedZonesCopy[zoneIndex].code = (option as {
      value: string;
      label: string;
      isDisabled?: boolean;
    })?.value;
    setSelectedZones(selectedZonesCopy);
  };
  const handleSubnetChange = (subnet: string, zoneIndex: number) => {
    const selectedZonesCopy = Array.from(selectedZones);
    selectedZonesCopy[zoneIndex].subnet = subnet;
    setSelectedZones(selectedZonesCopy);
  };
  const handleZoneDelete = (zoneIndex: number) => {
    const selectedZonesCopy = Array.from(selectedZones);
    selectedZonesCopy.splice(zoneIndex, 1);
    setSelectedZones(selectedZonesCopy);
  };

  const selectZoneCodeOptions = zoneCodeOptions?.map((zoneCode) => ({
    value: zoneCode,
    label: zoneCode,
    isDisabled: selectedZones?.find((zone) => zone.code === zoneCode) !== undefined
  }));
  return (
    <div className={clsx(className)}>
      <YBButton
        btnIcon="fa fa-plus"
        btnText="Add Zone"
        btnClass="btn btn-default"
        btnType="button"
        onClick={addZone}
        disabled={isSubmitting || zoneCodeOptions === undefined}
        data-testid="ConfigureAvailabilityZonField-AddZoneButton"
      />
      <div className={classes.zonesContainer}>
        {selectedZones.map((zone, index) => (
          <div className={classes.zoneConfigContainer} key={zone.code || index}>
            <div data-testid="ConfigureAvailabilityZonField-ZoneDropdown">
              <Select
                styles={zoneSelectStyles}
                options={selectZoneCodeOptions}
                onChange={(option) => {
                  handleZoneCodeChange(option, index);
                }}
                value={{ value: zone.code, label: zone.code, isDisabled: true }}
              />
            </div>
            <YBInput
              name="subnet"
              placeholder="Enter..."
              value={zone.subnet}
              onChange={(e) => handleSubnetChange(e.target.value, index)}
              fullWidth
              data-testid="ConfigureAvailabilityZonField-SubnetInput"
            />
            <YBButton
              className={classes.removeZoneButton}
              btnIcon="fa fa-trash-o"
              btnType="button"
              onClick={(e: any) => {
                handleZoneDelete(index);
                e.currentTarget.blur();
              }}
              data-testid="ConfigureAvailabilityZonField-DeleteZoneButton"
            />
          </div>
        ))}
      </div>
    </div>
  );
};
