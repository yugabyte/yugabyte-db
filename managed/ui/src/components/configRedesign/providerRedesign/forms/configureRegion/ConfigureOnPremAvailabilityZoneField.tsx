/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import clsx from 'clsx';
import { Box, makeStyles, Typography } from '@material-ui/core';
import { useFieldArray, useFormContext } from 'react-hook-form';

import { ConfigureOnPremRegionFormValues } from './ConfigureOnPremRegionModal';
import { OnPremRegionFieldLabel } from './constants';
import { YBButton } from '../../../../common/forms/fields';
import { YBInputField } from '../../../../../redesign/components';

interface ConfigureAvailabilityZoneFieldProps {
  isFormDisabled: boolean;
  className?: string;
}

const useStyles = makeStyles((theme) => ({
  formField: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(1),

    width: '100%'
  },
  zonesContainer: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(1)
  },
  addZoneButton: {
    marginTop: theme.spacing(1)
  },
  removeZoneButton: {
    backgroundColor: 'red',
    marginLeft: theme.spacing(1),
    padding: theme.spacing(1, 2, 1, 2),
    '& i': {
      color: 'white'
    },
    maxHeight: '42px'
  }
}));

export const ConfigureOnPremAvailabilityZoneField = ({
  isFormDisabled,
  className
}: ConfigureAvailabilityZoneFieldProps) => {
  const classes = useStyles();
  const { control } = useFormContext<ConfigureOnPremRegionFormValues>();
  const { fields, append, remove } = useFieldArray({ control, name: 'zones' });

  const addZoneField = () => {
    append({ code: '' });
  };
  return (
    <div className={clsx(className)}>
      <div className={classes.zonesContainer}>
        <Typography variant="h5">Availability Zones</Typography>
        {fields.map((zone, index) => (
          <div key={zone.id} className={classes.formField}>
            <div>{OnPremRegionFieldLabel.ZONE_NAME}</div>
            <Box display="flex">
              <YBInputField
                control={control}
                name={`zones.${index}.code`}
                placeholder="Enter..."
                disabled={isFormDisabled}
                fullWidth
              />
              <YBButton
                className={classes.removeZoneButton}
                btnIcon="fa fa-trash-o"
                onClick={() => remove(index)}
                disabled={isFormDisabled}
              />
            </Box>
          </div>
        ))}
        <YBButton
          className={classes.addZoneButton}
          btnIcon="fa fa-plus"
          btnText="Add Zone"
          btnClass="btn btn-default"
          btnType="button"
          onClick={addZoneField}
          disabled={isFormDisabled}
          data-testid="ConfigureOnPremAvailabilityZonField-AddZoneButton"
        />
      </div>
    </div>
  );
};
