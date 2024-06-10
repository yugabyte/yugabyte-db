/*
 * Created on Tue Jan 02 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { forwardRef, useCallback, useContext, useEffect, useImperativeHandle } from 'react';
import { toast } from 'react-toastify';
import { isEqual, noop, pick } from 'lodash';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { Grid, Typography, makeStyles } from '@material-ui/core';
import { FormProvider, useForm } from 'react-hook-form';

import DatabaseAPIField from './DatabaseAPIField';
import { BackupLocationField } from './BackupLocationField';
import StorageConfigField from './StorageConfigField';
import { DatabaseNameField } from './DatabaseNameField';
import { KMSConfigField } from './KMSConfigField';
import ParallelThreadsConfig from '../../restore/pages/generalSettings/ParallelThreadsConfig';
import { SelectTablesConfig } from '../../restore/pages/generalSettings/SelectTablesConfig';
import { TablespaceConfig } from '../../restore/pages/generalSettings/TablespaceConfig';
import { IGeneralSettings } from '../../restore/pages/generalSettings/GeneralSettings';
import { getPreflightCheck } from '../../restore/api';
import { isDefinedNotNull, isEmptyString } from '../../../../../utils/ObjectUtils';
import { TableType, TableTypeLabel } from '../../../../../redesign/helpers/dtos';
import { fetchTablesInUniverse } from '../../../../../actions/xClusterReplication';
import { isDuplicateKeyspaceExistsinUniverse } from '../../restore/RestoreUtils';

import { PageRef, RestoreContextMethods, RestoreFormContext } from '../../restore/RestoreContext';
import {
  AdvancedRestoreContextMethods,
  AdvancedRestoreFormContext
} from '../AdvancedRestoreContext';

type ReactSelectOption = { label: string; value: string } | null;

export type AdvancedGeneralConfigs = IGeneralSettings & {
  apiType: ReactSelectOption;
  backupLocation: string;
  backupStorageConfig: ReactSelectOption;
  databaseName: string;
  kmsConfig: ReactSelectOption;
};

const useStyles = makeStyles((theme) => ({
  root: {
    padding: '16px 8px',
    '& section': {
      marginBottom: '14px'
    },
    '& .yb-field-group': {
      paddingBottom: 0
    },
    '& .MuiInputLabel-root, .form-item-label': {
      textTransform: 'unset',
      fontWeight: 400,
      fontSize: '13px',
      color: theme.palette.ybacolors.labelBackground,
      marginBottom: theme.spacing(1)
    }
  },
  infoText: {
    lineHeight: '21px',
    marginBottom: '24px'
  },
  inputArea: {
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    borderRadius: `8px`,
    padding: `16px`,
    width: `560px`,
    marginTop: '16px'
  }
}));

const fieldsToCompare = ['apiType', 'backupLocation', 'backupStorageConfig', 'databaseName'];

const isValueUnChanged = (oldValues: AdvancedGeneralConfigs, newValues: AdvancedGeneralConfigs) => {
  return isEqual(pick(oldValues, fieldsToCompare), pick(newValues, fieldsToCompare));
};

// eslint-disable-next-line react/display-name
export const GeneralConfigurations = forwardRef<PageRef>((_, forwardRef) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'advancedRestoreModal.generalConfig'
  });

  const [
    ,
    { moveToNextPage, setDisableSubmit, setSubmitLabel, setisSubmitting }
  ]: AdvancedRestoreContextMethods = (useContext(
    AdvancedRestoreFormContext
  ) as unknown) as AdvancedRestoreContextMethods;

  const baseRestoreContext = (useContext(RestoreFormContext) as unknown) as RestoreContextMethods;

  const [
    {
      backupDetails,
      formData: { generalSettings, preflightResponse }
    },
    { savePreflightResponse, saveGeneralSettingsFormData, setBackupDetails }
  ] = baseRestoreContext;

  const classes = useStyles();

  const methods = useForm<AdvancedGeneralConfigs>({
    defaultValues: {
      ...generalSettings,
      backupLocation: '',
      databaseName: '',
      incrementalBackupProps: {
        ...generalSettings?.incrementalBackupProps,
        singleKeyspaceRestore: true
      },
      forceKeyspaceRename: false,
      apiType: {
        label: TableTypeLabel.PGSQL_TABLE_TYPE,
        value: TableType.PGSQL_TABLE_TYPE
      }
    },
    mode: 'onChange'
  });

  const saveValues = useCallback(
    (val: AdvancedGeneralConfigs, e) => {
      if (isValueUnChanged(generalSettings as any, val) && e !== undefined) return;
      const settings = {
        ...generalSettings,
        ...val
      };

      saveGeneralSettingsFormData(settings);
      const backupDetails = {
        backupType: val.apiType?.value,
        commonBackupInfo: {
          storageConfigUUID: val.backupStorageConfig?.value,
          responseList: [
            {
              defaultLocation: val.backupLocation,
              keyspace: val.databaseName
            }
          ]
        }
      };

      setBackupDetails(backupDetails as any);

      if (!e) {
        moveToNextPage({
          ...baseRestoreContext[0],
          formData: {
            ...baseRestoreContext[0].formData,
            generalSettings: settings
          }
        });
      }
    },
    [generalSettings, baseRestoreContext]
  );
  const { watch } = methods;

  const formValues = methods.getValues();

  const { isFetching, isSuccess: isPreflightSuccess } = useQuery(
    ['backup', 'preflight', pick(formValues, fieldsToCompare)],
    () =>
      getPreflightCheck({
        backupUUID: undefined as any,
        storageConfigUUID: backupDetails!.commonBackupInfo.storageConfigUUID,
        universeUUID: formValues.targetUniverse!.value!,
        backupLocations: backupDetails!.commonBackupInfo.responseList.map((r) => r.defaultLocation!)
      }),
    {
      enabled:
        isDefinedNotNull(formValues?.targetUniverse?.value) &&
        isDefinedNotNull(backupDetails) &&
        backupDetails?.commonBackupInfo.storageConfigUUID !== undefined &&
        !isEmptyString(formValues.backupLocation) &&
        !isEmptyString(formValues.databaseName),
      onSuccess(data) {
        savePreflightResponse(data);
        setDisableSubmit(false);
      },
      onError: () => {
        toast.error('Preflight check failed!.');
        setDisableSubmit(true);
      },
      onSettled: () => {
        setisSubmitting(false);
      }
    }
  );

  // we do duplicate check only for YSQL and when the preflight check is finished
  const enableVerifyDuplicateTable =
    formValues?.apiType?.value === TableType.PGSQL_TABLE_TYPE &&
    isPreflightSuccess &&
    isDefinedNotNull(preflightResponse);

  useQuery(
    ['tables', generalSettings?.targetUniverse, preflightResponse],
    () => fetchTablesInUniverse(generalSettings!.targetUniverse?.value),
    {
      enabled: enableVerifyDuplicateTable,
      onSuccess: (resp) => {
        if (isDuplicateKeyspaceExistsinUniverse(preflightResponse, resp.data)) {
          methods.setValue('forceKeyspaceRename', true);
          methods.setValue('renameKeyspace', true);
        }
      }
    }
  );

  // disables the submit button to prevent the user from moving to next page,
  // till the preflight check is finished.
  useEffect(() => {
    setDisableSubmit(true);
    savePreflightResponse(undefined);
    methods.setValue('useTablespaces', false);
  }, [setDisableSubmit, formValues.targetUniverse]);

  useEffect(() => {
    if (isFetching) {
      setDisableSubmit(true);
      setisSubmitting(true);
      setSubmitLabel(t('verifying', { keyPrefix: 'newRestoreModal.generalSettings' }));
    }
  }, [isFetching]);

  const renameKeyspace = watch('renameKeyspace');
  const tableSelectionType = watch('tableSelectionType');

  // if the user chooses rename keyspaces, or "subset of tables",
  // change the modal's submit button to 'Next'
  useEffect(() => {
    if (isFetching) return;
    if (renameKeyspace || tableSelectionType === 'SUBSET_OF_TABLES') {
      setSubmitLabel(t('next', { keyPrefix: 'newRestoreModal.generalSettings' }));
    } else {
      setSubmitLabel(t('restore', { keyPrefix: 'newRestoreModal.generalSettings' }));
    }
  }, [renameKeyspace, setSubmitLabel, tableSelectionType, t, isFetching]);

  const { handleSubmit } = methods;
  // when the modal's submit button is clicked , save the form values.
  const onNext = useCallback(() => handleSubmit(saveValues)(), [handleSubmit, saveValues]);

  useImperativeHandle(forwardRef, () => ({ onNext, onPrev: noop }), [onNext]);

  return (
    <FormProvider {...methods}>
      <div className={classes.root}>
        <section>
          <Grid>
            <Grid item xs={11}>
              <Typography variant="body2" className={classes.infoText}>
                {t('subTitle')}
              </Typography>
            </Grid>
          </Grid>
        </section>
        <div>
          <Typography variant="h5">{t('sectionTitle')}</Typography>
          <div className={classes.inputArea} onBlur={(e) => handleSubmit(saveValues)(e)}>
            <section>
              <DatabaseAPIField />
            </section>
            <section>
              <BackupLocationField />
            </section>
            <section>
              <StorageConfigField />
            </section>
            <section>
              <DatabaseNameField />
            </section>
            <section>
              <KMSConfigField />
            </section>
            <section onBlur={(e) => e.stopPropagation()}>
              <SelectTablesConfig />
            </section>
            <section>
              <TablespaceConfig />
            </section>
            {preflightResponse?.backupCategory === 'YB_BACKUP_SCRIPT' && (
              <section>
                <ParallelThreadsConfig />
              </section>
            )}
          </div>
        </div>
      </div>
    </FormProvider>
  );
});
