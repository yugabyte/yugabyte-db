/*
 * Created on Mon Nov 20 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import clsx from 'clsx';
import { useTranslation } from 'react-i18next';
import { values } from 'lodash';
import { Control, useFieldArray, useForm } from 'react-hook-form';
import { FormHelperText, Grid, Typography, makeStyles } from '@material-ui/core';
import { yupResolver } from '@hookform/resolvers/yup';
import {
  RadioGroupOrientation,
  YBInputField,
  YBModal,
  YBRadioGroupField
} from '../../../../../redesign/components';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { CloudVendorRegionField } from '../../forms/configureRegion/ConfigureRegionModal';
import { isNonEmptyObject } from '../../../../../utils/ObjectUtils';
import {
  ImageBundle,
  ImageBundleType
} from '../../../../../redesign/features/universe/universe-form/utils/dto';
import { ArchitectureType, ProviderCode } from '../../constants';
import { AWSProviderEditFormFieldValues } from '../../forms/aws/AWSProviderEditForm';
import { AWSProviderCreateFormFieldValues } from '../../forms/aws/AWSProviderCreateForm';
import { getAddLinuxVersionSchema } from './ValidationSchemas';

import styles from '../RegionList.module.scss';

interface AddLinuxVersionModalProps {
  providerType: ProviderCode;
  control: Control<AWSProviderCreateFormFieldValues | AWSProviderEditFormFieldValues>;
  visible: boolean;
  onHide: () => void;
  onSubmit: (values: ImageBundle) => void;
  editDetails?: ImageBundle;
  existingImageBundles?: ImageBundle[];
}

interface ImageBundleExtendedProps {
  machineImageId: string;
}

const useStyles = makeStyles((theme) => ({
  root: {
    padding: '24px 32px'
  },
  icon: {
    color: theme.palette.ybacolors.ybIcon
  },
  form: {
    counterReset: 'id-counter 0',
    '&>div': {
      counterIncrement: 'id-counter 1',
      marginBottom: '40px',
      '&>p': {
        marginBottom: '16px'
      },
      '&>p:before': {
        content: `counter(id-counter) ". "`
      }
    }
  },
  nameInput: {
    width: '380px'
  },
  regions: {
    borderRadius: 8,
    border: `1px solid #E3E3E5`,
    background: theme.palette.common.white,
    padding: '21px'
  },
  amiInput: {
    width: '230px'
  },
  ybaActiveError: {
    marginBottom: '10px'
  }
}));

export const AddLinuxVersionModal: FC<AddLinuxVersionModalProps> = ({
  providerType,
  control,
  visible,
  onHide,
  onSubmit,
  editDetails = {},
  existingImageBundles = []
}) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'linuxVersion'
  });
  const classes = useStyles();

  const regions = useFieldArray({
    name: 'regions',
    control
  });

  const isEditMode = isNonEmptyObject(editDetails);

  const isYBAManagedBundle =
    isNonEmptyObject(editDetails) &&
    (editDetails as ImageBundle)?.metadata?.type === ImageBundleType.YBA_ACTIVE;

  const { control: formControl, handleSubmit, reset } = useForm<
    ImageBundle & ImageBundleExtendedProps
  >({
    defaultValues: {
      details: {
        arch: ArchitectureType.X86_64,
        sshPort: 22
      },
      ...editDetails
    },
    resolver: yupResolver(
      getAddLinuxVersionSchema(
        providerType,
        t,
        existingImageBundles as any,
        isEditMode,
        isYBAManagedBundle
      )
    )
  });

  const CPU_ARCH_OPTIONS = [
    {
      value: ArchitectureType.X86_64,
      label: t('x86_64', { keyPrefix: 'universeForm.instanceConfig' })
    },
    {
      value: ArchitectureType.ARM64,
      label: t('aarch64', { keyPrefix: 'universeForm.instanceConfig' })
    }
  ];

  if (!visible) return null;

  return (
    <YBModal
      open={visible}
      onClose={() => {
        reset();
        onHide();
      }}
      title={isEditMode ? t('editLinuxVersion') : t('addLinuxVersion')}
      titleIcon={<i className={clsx('fa fa-plus', classes.icon)} />}
      dialogContentProps={{
        className: classes.root,
        dividers: true
      }}
      submitLabel={isEditMode ? t('editLinuxVersion') : t('addLinuxVersion')}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
      overrideWidth={'800px'}
      overrideHeight={'810px'}
      onSubmit={() => {
        handleSubmit((values) => {
          onSubmit(values);
          reset();
        })();
      }}
    >
      {isYBAManagedBundle && (
        <FormHelperText className={classes.ybaActiveError} error={true}>
          {t('form.validationMsg.cannotModfiyYBABundles')}
        </FormHelperText>
      )}

      <div className={classes.form}>
        <div>
          <Typography variant="body1">{t('form.linuxVersionName')}</Typography>
          <YBInputField
            control={formControl}
            name="name"
            className={classes.nameInput}
            placeholder={t('form.linuxVersionNamePlaceholder')}
            disabled={isEditMode || isYBAManagedBundle}
          />
        </div>
        {providerType !== ProviderCode.AWS && (
          <div>
            <Typography variant="body1">{t('form.machineImageId')}</Typography>
            <YBInputField
              control={formControl}
              name={`details.globalYbImage`}
              className={classes.nameInput}
              placeholder={t('form.machineImageIdPlaceholder')}
              disabled={isYBAManagedBundle}
            />
          </div>
        )}
        {providerType === ProviderCode.AWS && (
          <div>
            <Typography variant="body1">{t('form.cpuArch')}</Typography>
            <YBRadioGroupField
              control={formControl}
              options={CPU_ARCH_OPTIONS}
              name="details.arch"
              orientation={RadioGroupOrientation.HORIZONTAL}
              isDisabled={isEditMode || isYBAManagedBundle}
            />
          </div>
        )}
        {providerType === ProviderCode.AWS && (
          <div>
            <Typography variant="body1">{t('form.amazonMachineImage')}</Typography>
            <div>
              <div className={clsx(styles.bootstrapTableContainer, classes.regions)}>
                <BootstrapTable tableContainerClass={styles.bootstrapTable} data={regions.fields}>
                  <TableHeaderColumn dataField="uuid" isKey={true} hidden={true} />
                  <TableHeaderColumn dataField="name">{t('form.region')}</TableHeaderColumn>
                  <TableHeaderColumn
                    dataField="ami_id"
                    dataFormat={(_, cell: CloudVendorRegionField) => {
                      return (
                        <YBInputField
                          control={formControl}
                          name={`details.regions.${cell.code}.ybImage`}
                          placeholder={t('form.machineImagePlaceholder')}
                          className={classes.amiInput}
                          disabled={isYBAManagedBundle}
                        />
                      );
                    }}
                  >
                    {t('form.amiId')}
                  </TableHeaderColumn>
                </BootstrapTable>
              </div>
            </div>
          </div>
        )}

        <div>
          <Typography variant="body1">{t('form.portDetails')}</Typography>
          <Grid container spacing={3} alignItems="center">
            <Grid item xs={3}>
              {t('form.sshUser')}
            </Grid>
            <Grid item xs={9}>
              <YBInputField
                control={formControl}
                name={'details.sshUser'}
                placeholder={t('form.sshUserPlaceholder')}
                fullWidth
                disabled={isYBAManagedBundle}
              />
            </Grid>
          </Grid>
          <Grid container spacing={3} alignItems="center">
            <Grid item xs={3}>
              {t('form.sshPort')}
            </Grid>
            <Grid item xs={9}>
              <YBInputField
                type="number"
                control={formControl}
                name={'details.sshPort'}
                placeholder={t('form.sshPortPlaceholder')}
                disabled={isYBAManagedBundle}
                fullWidth
              />
            </Grid>
          </Grid>
        </div>
      </div>
    </YBModal>
  );
};
