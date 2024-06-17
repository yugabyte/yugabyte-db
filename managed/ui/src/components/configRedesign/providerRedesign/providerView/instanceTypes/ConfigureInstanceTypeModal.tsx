/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import clsx from 'clsx';
import { FormProvider, SubmitHandler, useForm } from 'react-hook-form';
import { makeStyles, Typography } from '@material-ui/core';
import { useQuery } from 'react-query';

import { YBInputField, YBModal, YBModalProps } from '../../../../../redesign/components';
import { InstanceTypeOperation, InstanceTypeOperationLabel } from '../../constants';
import { api, instanceTypeQueryKey } from '../../../../../redesign/helpers/api';
import { YBLoading } from '../../../../common/indicators';

interface ConfigureInstanceTypeModalProps extends YBModalProps {
  instanceTypeOperation: InstanceTypeOperation;
  onInstanceTypeSubmit: (instance: ConfigureInstanceTypeFormValues) => void;
  onClose: () => void;
  providerUuid: string;
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
  providerUuid,
  onClose,
  onInstanceTypeSubmit,
  ...modalProps
}: ConfigureInstanceTypeModalProps) => {
  const formMethods = useForm<ConfigureInstanceTypeFormValues>();
  const classes = useStyles();

  const instanceTypeQuery = useQuery(instanceTypeQueryKey.provider(providerUuid), () =>
    api.fetchInstanceTypes(providerUuid)
  );
  const onSubmit: SubmitHandler<ConfigureInstanceTypeFormValues> = (formValues) => {
    onInstanceTypeSubmit(formValues);
    formMethods.reset();
    onClose();
  };
  if (instanceTypeQuery.isLoading) {
    <YBModal
      title={`${InstanceTypeOperationLabel[instanceTypeOperation]} Instance Type`}
      titleIcon={<i className={clsx('fa fa-plus', classes.titleIcon)} />}
      onClose={onClose}
      {...modalProps}
    >
      <YBLoading />
    </YBModal>;
  }

  const inUseInstanceTypeCodes = (instanceTypeQuery.data ?? []).map(
    (instanceType) => instanceType.instanceTypeCode
  );
  return (
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
        <Typography variant="body2">Name</Typography>
        <YBInputField
          control={formMethods.control}
          name="instanceTypeCode"
          rules={{
            validate: {
              required: (instanceTypeCode) =>
                !!instanceTypeCode || 'Instance type name is required',
              notInUse: (instanceTypeCode) =>
                !inUseInstanceTypeCodes.includes(instanceTypeCode.toString()) ||
                'This instance type name is already in use.'
            }
          }}
          fullWidth
        />
      </div>
      <div className={classes.formField}>
        <Typography variant="body2">Number of Cores</Typography>
        <YBInputField
          control={formMethods.control}
          name="numCores"
          type="number"
          inputProps={{ min: 1 }}
          rules={{
            required: 'Number of cores is required',
            min: { value: 1, message: 'Minimum of 1 core.' }
          }}
          fullWidth
        />
      </div>
      <div className={classes.formField}>
        <Typography variant="body2">Memory Size (GB)</Typography>
        <YBInputField
          control={formMethods.control}
          name="memSizeGB"
          type="number"
          inputProps={{ min: 0 }}
          rules={{
            required: 'Memory size is required.',
            min: { value: 0, message: 'Memory size must be a positive value.' }
          }}
          fullWidth
        />
      </div>
      <div className={classes.formField}>
        <Typography variant="body2">Volume Size (GB)</Typography>
        <YBInputField
          control={formMethods.control}
          name="volumeSizeGB"
          type="number"
          inputProps={{ min: 0 }}
          rules={{
            required: 'Volume size is required.',
            min: { value: 0, message: 'Volume size must be a positive value.' }
          }}
          fullWidth
        />
      </div>
      <div className={classes.formField}>
        <Typography variant="body2">Mount Paths (Comma Separated)</Typography>
        <YBInputField
          control={formMethods.control}
          name="mountPaths"
          rules={{ required: 'Mount paths are required.' }}
          fullWidth
        />
      </div>
    </YBModal>
  );
};
