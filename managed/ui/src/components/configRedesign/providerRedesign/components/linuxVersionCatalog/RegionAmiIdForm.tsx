/*
 * Created on Wed Dec 06 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import clsx from 'clsx';
import { useFormContext } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { Typography, makeStyles } from '@material-ui/core';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { ConfigureRegionFormValues } from '../../forms/configureRegion/ConfigureRegionModal';
import { YBInputField } from '../../../../../redesign/components';
import { ImageBundleDefaultTag } from './LinuxVersionUtils';

import { CloudVendorProviders } from '../../constants';
import {
  ImageBundle,
  ImageBundleType
} from '../../../../../redesign/features/universe/universe-form/utils/dto';

import styles from '../RegionList.module.scss';

interface RegionAmiIdFormProps {
  providerType: typeof CloudVendorProviders[number];
}

const useStyles = makeStyles((theme) => ({
  amiRegion: {
    borderRadius: 8,
    border: `1px solid #E3E3E5`,
    background: theme.palette.common.white,
    padding: '21px',
    marginTop: '14px'
  },
  amiInput: {
    width: '230px'
  },
  linuxName: {
    display: 'flex',
    alignItems: 'center',
    gap: '8px'
  },
  hideRow: {
    display: 'none'
  }
}));

export const RegionAmiIdForm: FC<RegionAmiIdFormProps> = ({ providerType }) => {
  const classes = useStyles();
  const { t } = useTranslation('translation', { keyPrefix: 'linuxVersion.regionAmiModal' });
  const { getValues, control } = useFormContext<ConfigureRegionFormValues>();

  const regionData = getValues().regionData;
  const imageBundles = getValues().imageBundles ?? [];
  if (!regionData) return null;

  return (
    <>
      <Typography variant="h5">{t('title', { region_name: regionData.label })}</Typography>
      <div className={clsx(styles.bootstrapTableContainer, classes.amiRegion)}>
        <BootstrapTable
          tableContainerClass={styles.bootstrapTable}
          data={imageBundles}
          trClassName={(row: ImageBundle) => {
            return row.metadata.type !== ImageBundleType.CUSTOM ? classes.hideRow : '';
          }}
        >
          <TableHeaderColumn dataField="uuid" isKey={true} hidden={true} />
          <TableHeaderColumn
            dataField="name"
            dataFormat={(name, row: ImageBundle) => (
              <div className={classes.linuxName}>
                {name}
                <ImageBundleDefaultTag
                  icon={<></>}
                  tooltip=""
                  text={t(row.details.arch, { keyPrefix: 'universeForm.instanceConfig' })}
                />
              </div>
            )}
          >
            {t('linuxVersion', { keyPrefix: 'universeForm.instanceConfig' })}
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="ami_id"
            dataFormat={(_, _row, _a, rowIndex) => {
              return (
                <YBInputField
                  control={control}
                  name={`imageBundles.${rowIndex}.details.regions.${regionData.value.code}.ybImage`}
                  placeholder={t('form.machineImagePlaceholder', { keyPrefix: 'linuxVersion' })}
                  className={classes.amiInput}
                />
              );
            }}
          >
            {t('regionAmiId')}
          </TableHeaderColumn>
        </BootstrapTable>
      </div>
    </>
  );
};
