/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import React from 'react';
import clsx from 'clsx';
import { Divider, makeStyles, Typography } from '@material-ui/core';
import { YBButton } from '../../../../common/forms/fields';
import { useFieldArray, useFormContext } from 'react-hook-form';

import { K8sRegionField } from './ConfigureK8sRegionModal';
import {
  K8sCertIssuerType,
  K8sCertIssuerTypeLabel,
  K8sRegionFieldLabel,
  RegionOperation
} from './constants';
import {
  OptionProps,
  YBInput,
  YBInputField,
  YBRadioGroupField,
  YBToggleField
} from '../../../../../redesign/components';
import { YBDropZoneField } from '../../components/YBDropZone/YBDropZoneField';
import { YBTextAreaField } from '../../../../../redesign/components/YBInput/YBTextAreaField';

interface ConfigureK8sAvailabilityZoneFieldProps {
  isFormDisabled: boolean;
  regionOperation: RegionOperation;
  inUseZones: Set<String>;

  className?: string;
}

const useStyles = makeStyles((theme) => ({
  formField: {
    marginTop: theme.spacing(1),
    '&:first-child': {
      marginTop: 0
    }
  },
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
  addZoneButton: {
    marginTop: theme.spacing(1)
  },
  removeZoneButton: {
    marginTop: theme.spacing(1),
    padding: theme.spacing(1, 2, 1, 2),
    backgroundColor: 'red',
    color: 'white',
    '& i': {
      color: 'white'
    }
  }
}));

const CERT_ISSUER_TYPE_OPTIONS: OptionProps[] = [
  {
    value: K8sCertIssuerType.NONE,
    label: K8sCertIssuerTypeLabel[K8sCertIssuerType.NONE]
  },
  {
    value: K8sCertIssuerType.ISSUER,
    label: K8sCertIssuerTypeLabel[K8sCertIssuerType.ISSUER]
  },
  {
    value: K8sCertIssuerType.CLUSTER_ISSUER,
    label: K8sCertIssuerTypeLabel[K8sCertIssuerType.CLUSTER_ISSUER]
  }
];

export const ConfigureK8sAvailabilityZoneField = ({
  regionOperation,
  isFormDisabled,
  inUseZones,
  className
}: ConfigureK8sAvailabilityZoneFieldProps) => {
  const classes = useStyles();
  const { control, watch } = useFormContext<K8sRegionField>();
  const { fields, append, remove } = useFieldArray({ control, name: 'zones' });

  const addZoneField = () => {
    append({
      code: '',
      certIssuerType: K8sCertIssuerType.NONE,
      editKubeConfigContent: true,
      isNewZone: true
    });
  };
  const zones = watch('zones', []);
  return (
    <div className={clsx(className)}>
      <div className={classes.zonesContainer}>
        <Typography variant="h5">Availability Zones</Typography>
        {fields.map((zone, index) => {
          // TODO: We might be able to simplify this to `!!zones[index].kubeConfigFilepath`
          const hasExistingKubeConfig =
            regionOperation === RegionOperation.EDIT_EXISTING &&
            !zones[index].isNewZone &&
            !!zones[index].kubeConfigFilepath;
          const isZoneInUse = zone?.code !== undefined && inUseZones.has(zone.code);
          const isFieldDisabled = isZoneInUse || isFormDisabled;
          return (
            <div key={zone.id}>
              {index !== 0 && <Divider />}
              <div className={classes.formField}>
                <div>{K8sRegionFieldLabel.ZONE_CODE}</div>
                <YBInputField
                  control={control}
                  name={`zones.${index}.code`}
                  placeholder="Enter..."
                  disabled={isFieldDisabled}
                  fullWidth
                />
              </div>
              {hasExistingKubeConfig && (
                <>
                  <div className={classes.formField}>
                    <div>{K8sRegionFieldLabel.CURRENT_KUBE_CONFIG_FILEPATH}</div>
                    <YBInput value={zones[index].kubeConfigFilepath} disabled={true} fullWidth />
                  </div>
                  <div className={classes.formField}>
                    <div>{K8sRegionFieldLabel.EDIT_KUBE_CONFIG}</div>
                    <YBToggleField
                      name={`zones.${index}.editKubeConfigContent`}
                      control={control}
                      disabled={isFieldDisabled}
                    />
                  </div>
                </>
              )}
              {(!hasExistingKubeConfig || zones[index].editKubeConfigContent) && (
                <div className={classes.formField}>
                  <div>{K8sRegionFieldLabel.KUBE_CONFIG_CONTENT}</div>
                  <YBDropZoneField
                    name={`zones.${index}.kubeConfigContent`}
                    control={control}
                    actionButtonText="Upload Kube Config File"
                    multipleFiles={false}
                    showHelpText={false}
                    disabled={isFieldDisabled}
                  />
                </div>
              )}
              <div className={classes.formField}>
                <div>{K8sRegionFieldLabel.STORAGE_CLASSES}</div>
                <YBInputField
                  control={control}
                  name={`zones.${index}.kubernetesStorageClass`}
                  placeholder="Enter..."
                  disabled={isFieldDisabled}
                  fullWidth
                />
              </div>
              <div className={classes.formField}>
                <div>{K8sRegionFieldLabel.KUBE_POD_ADDRESS_TEMPLATE}</div>
                <YBInputField
                  control={control}
                  name={`zones.${index}.kubePodAddressTemplate`}
                  placeholder="Enter..."
                  disabled={isFieldDisabled}
                  fullWidth
                />
              </div>
              <div className={classes.formField}>
                <div>{K8sRegionFieldLabel.KUBE_DOMAIN}</div>
                <YBInputField
                  control={control}
                  name={`zones.${index}.kubeDomain`}
                  placeholder="Enter..."
                  disabled={isFieldDisabled}
                  fullWidth
                />
              </div>
              <div className={classes.formField}>
                <div>{K8sRegionFieldLabel.KUBE_NAMESPACE}</div>
                <YBInputField
                  control={control}
                  name={`zones.${index}.kubeNamespace`}
                  placeholder="Enter..."
                  disabled={isFieldDisabled}
                  fullWidth
                />
              </div>
              <div className={classes.formField}>
                <div>{K8sRegionFieldLabel.OVERRIDES}</div>
                <YBTextAreaField
                  control={control}
                  name={`zones.${index}.overrides`}
                  disabled={isFieldDisabled}
                />
              </div>
              <div className={classes.formField}>
                <div>{K8sRegionFieldLabel.CERT_ISSUER_TYPE}</div>
                <YBRadioGroupField
                  control={control}
                  name={`zones.${index}.certIssuerType`}
                  options={CERT_ISSUER_TYPE_OPTIONS}
                  orientation="horizontal"
                  isDisabled={isFieldDisabled}
                />
              </div>
              {([
                K8sCertIssuerType.CLUSTER_ISSUER,
                K8sCertIssuerType.ISSUER
              ] as K8sCertIssuerType[]).includes(zones?.[index].certIssuerType) && (
                <div className={classes.formField}>
                  <div>{K8sRegionFieldLabel.CERT_ISSUER_NAME}</div>
                  <YBInputField
                    control={control}
                    name={`zones.${index}.certIssuerName`}
                    placeholder="Enter..."
                    disabled={isFieldDisabled}
                    fullWidth
                  />
                </div>
              )}
              <YBButton
                className={classes.removeZoneButton}
                btnIcon="fa fa-trash-o"
                btnText="Delete Zone"
                onClick={() => remove(index)}
                disabled={isFieldDisabled}
              />
            </div>
          );
        })}
        <YBButton
          className={classes.addZoneButton}
          btnIcon="fa fa-plus"
          btnText="Add Zone"
          btnClass="btn btn-default"
          btnType="button"
          onClick={addZoneField}
          disabled={isFormDisabled}
          data-testid="ConfigureK8sAvailabilityZoneField-AddZoneButton"
        />
      </div>
    </div>
  );
};
