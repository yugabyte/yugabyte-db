/*
 * Created on Fri Feb 09 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { forwardRef, useEffect } from 'react';
import * as Yup from 'yup';
import { useQuery } from 'react-query';
import { useFieldArray, useForm } from 'react-hook-form';
import { yupResolver } from '@hookform/resolvers/yup';
import { Box, MenuItem, IconButton, makeStyles, Slide } from '@material-ui/core';
import { YBModal, YBButton, YBInputField, YBSelectField } from '../../../redesign/components';
import { YBA_ROLES } from './LDAPAuth';
import { YBLoadingCircleIcon } from '../../common/indicators';
import { getAllRoles } from '../../../redesign/features/rbac/api';
import TrashIcon from '../icons/trash_icon';
import RemoveIcon from '../icons/remove_icon';
import AddIcon from '../icons/add_icon';

const useStyles = makeStyles((theme) => ({
  deleteBtn: {
    height: 40
  },
  mapTable: {
    background: theme.palette.ybacolors.backgroundGrayLightest,
    border: `1px solid #CCCCCC`,
    borderRadius: theme.spacing(1),
    padding: theme.spacing(3)
  },
  addRowBtn: {
    padding: 0
  }
}));

//validation
Yup.addMethod(Yup.array, 'unique', function (field, message) {
  return this.test('unique', message, function (array) {
    const uniqueData = Array.from(new Set(array.map((row) => row[field]?.toLowerCase())));
    const isUnique = array.length === uniqueData.length;
    if (isUnique) {
      return true;
    }
    const index = array.findIndex((row, i) => row[field]?.toLowerCase() !== uniqueData[i]);
    if (array[index][field] === '') {
      return true;
    }
    return this.createError({
      path: `${this.path}.${index}.${field}`,
      message
    });
  });
});

const schema = Yup.object().shape({
  mapping: Yup.array()
    .unique('groupName', 'Group Name must be unique.')
    .of(
      Yup.object().shape({
        ybaRole: Yup.string().required('Role is required.'),
        groupName: Yup.string().required('Group Name is required')
      })
    )
});
//validation

const DEFAULT_MAPPING_VALUE = [
  {
    ybaRole: 'Admin',
    groupName: ''
  },
  {
    ybaRole: 'BackupAdmin',
    groupName: ''
  },
  {
    ybaRole: 'ReadOnly',
    groupName: ''
  }
];

const Transition = forwardRef(function Transition(props, ref) {
  return <Slide direction="up" ref={ref} {...props} />;
});

export const OIDCMappingModal = ({ open, onClose, onSubmit, values }) => {
  const classes = useStyles();

  const {
    control,
    getValues,
    setValue,
    trigger,
    formState: { isValid }
  } = useForm({
    mode: 'onChange',
    defaultValues: {
      mapping: values && values.length ? values : DEFAULT_MAPPING_VALUE
    },
    resolver: yupResolver(schema)
  });
  const { fields, append, remove, replace } = useFieldArray({
    control,
    name: 'mapping'
  });

  const { data: roles, isLoading } = useQuery('roles', getAllRoles, {
    enabled: open,
    select: resp => resp.data
  });

  useEffect(() => {
    if(!roles) return;
    const val = values.map(v => ({
      ...v,
      ybaRole: roles.find(r => r.roleUUID === v.roles[0])?.name
    }));
    replace(val);
  }, [roles]);

  const handleFormSubmit = () => {
    if (!isValid) trigger();
    else {
      const mappings = getValues('mapping').map(m => ({
        groupName: m.groupName,
        roles: [roles.find(r => m.ybaRole === r.name)?.roleUUID]
      }));
      onSubmit(mappings);
    };
  };


  if (isLoading) return <YBLoadingCircleIcon />;


  return (
    <YBModal
      open={open}
      overrideWidth={1110}
      overrideHeight="auto"
      titleSeparator
      cancelLabel="Cancel"
      submitLabel="Confirm"
      size="lg"
      title="Create Mapping"
      onClose={onClose}
      onSubmit={handleFormSubmit}
      dialogContentProps={{ style: { paddingTop: 20 } }}
      submitTestId="submit-oidc-groups"
      cancelTestId="close-oidc-groups"
      TransitionComponent={Transition}
    >
      <Box
        display="flex"
        width="100%"
        flexDirection="column"
        data-testid="oidc-groups-modal"
        mb={2}
      >
        <Box mt={0.5}>
          Add OIDC Groups you want to map to the following role.
        </Box>
        <Box mt={1} display="flex" justifyContent="flex-end">
          <YBButton
            variant="secondary"
            className={classes.deleteBtn}
            disabled={!values.length}
            onClick={() => {
              setValue('mapping', []);
            }}
          >
            <TrashIcon /> &nbsp;
            <>Delete All Mappings </>
          </YBButton>
        </Box>

        <Box mt={1} display="flex" className={classes.mapTable} flexDirection="column">
          {fields.length > 0 && (
            <Box display="flex" mb={0.5}>
              <Box display="flex" width={160}>
                Role
              </Box>
              <Box ml={1} display="flex" width="100%">
                Group Name
              </Box>
              <Box display="flex" width={24} mx={1}></Box>
            </Box>
          )}
          <Box>
            {fields.map((field, index) => {
              return (
                <Box display="flex" alignItems="center" mb={1} key={field.id}>
                  <Box display="flex" width={160}>
                    <YBSelectField fullWidth name={`mapping.${index}.ybaRole`} control={control}>
                      {YBA_ROLES.map((role) => (
                        <MenuItem key={role.value} value={role.value}>
                          {role.label}
                        </MenuItem>
                      ))}
                    </YBSelectField>
                  </Box>
                  <Box ml={1} display="flex" width="100%">
                    <YBInputField
                      fullWidth
                      name={`mapping.${index}.groupName`}
                      control={control}
                    />
                  </Box>
                  <Box display="flex" width={24} mx={1}>
                    <IconButton onClick={() => remove(index)}>
                      <RemoveIcon />
                    </IconButton>
                  </Box>
                </Box>
              );
            })}
          </Box>
          <Box mt={1}>
            <YBButton
              variant="ghost"
              onClick={() => append({ ybaRole: '', groupName: '' }, { shouldFocus: false })}
              className={classes.addRowBtn}
            >
              <AddIcon /> &nbsp;
              <>Add rows </>
            </YBButton>
          </Box>
        </Box>
      </Box>
    </YBModal>
  );
};
