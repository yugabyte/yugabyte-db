/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import React from 'react';
import clsx from 'clsx';
import { FormProvider, SubmitHandler, useForm } from 'react-hook-form';
import { makeStyles, Typography } from '@material-ui/core';

import { YBInputField, YBModal, YBModalProps } from '../../../../../redesign/components';
import { InstanceTypeOperation, InstanceTypeOperationLabel } from '../../constants';

interface ConfigureInstanceTypeModalProps extends YBModalProps {
  onInstanceTypeSubmit: (instance: ConfigureInstanceTypeFormValues) => void;
  onClose: () => void;
  instanceTypeOperation: InstanceTypeOperation;
}

export interface ConfigureInstanceTypeFormValues {
  instanceTypeCode: string;
  numCores: number;
  memSizeGB: number;
  volumeSizeGB: number;
  mountPaths: string;
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
  }
}));

export const ConfigureInstanceTypeModal = ({
  instanceTypeOperation,
  onClose,
  onInstanceTypeSubmit,
  ...modalProps
}: ConfigureInstanceTypeModalProps) => {
  const formMethods = useForm<ConfigureInstanceTypeFormValues>();
  const classes = useStyles();

  const onSubmit: SubmitHandler<ConfigureInstanceTypeFormValues> = (formValues) => {
    onInstanceTypeSubmit(formValues);
    formMethods.reset();
    onClose();
  };
  return (
    <FormProvider {...formMethods}>
      <YBModal
        title={`${InstanceTypeOperationLabel[instanceTypeOperation]} Instance Type`}
        titleIcon={<i className={clsx('fa fa-plus', classes.titleIcon)} />}
        submitLabel={`${InstanceTypeOperationLabel[instanceTypeOperation]} Instance Type`}
        cancelLabel="Cancel"
        onSubmit={formMethods.handleSubmit(onSubmit)}
        onClose={onClose}
        {...modalProps}
      >
        <div className={classes.formField}>
          <Typography variant="body2">Machine Type</Typography>
          <YBInputField control={formMethods.control} name="instanceTypeCode" fullWidth />
        </div>
        <div className={classes.formField}>
          <Typography variant="body2">Number of Cores</Typography>
          <YBInputField control={formMethods.control} name="numCores" type="number" fullWidth />
        </div>
        <div className={classes.formField}>
          <Typography variant="body2">Memory Size (GB)</Typography>
          <YBInputField control={formMethods.control} name="memSizeGB" type="number" fullWidth />
        </div>
        <div className={classes.formField}>
          <Typography variant="body2">Volume Size (GB)</Typography>
          <YBInputField control={formMethods.control} name="volumeSizeGB" type="number" fullWidth />
        </div>
        <div className={classes.formField}>
          <Typography variant="body2">Mount Paths (Comma Separated)</Typography>
          <YBInputField control={formMethods.control} name="mountPaths" fullWidth />
        </div>
      </YBModal>
    </FormProvider>
  );
};
