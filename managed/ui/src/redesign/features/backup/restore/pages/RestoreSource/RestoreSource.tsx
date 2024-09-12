/*
 * Created on Tue Aug 20 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { forwardRef, useEffect, useImperativeHandle } from 'react';
import { useMount } from 'react-use';
import { useTranslation } from 'react-i18next';
import { toast } from 'react-toastify';
import { useMutation } from 'react-query';
import { useFormContext } from 'react-hook-form';
import { Divider, makeStyles } from '@material-ui/core';

import { Page, PageRef } from '../../models/RestoreContext';
import { SelectDBAndTables } from './SelectDBAndTables';
import { SelectTimeframe } from './SelectTimeframe';
import { GetRestoreContext, prepareValidationPayload } from '../../RestoreUtils';
import { createErrorMessage } from '../../../../universe/universe-form/utils/helpers';
import { validateRestorableTables, ValidateRestoreApiReq } from '../../api/api';
import { RestoreFormModel } from '../../models/RestoreFormModel';

const useStyles = makeStyles(() => ({
  root: {
    padding: '24px'
  },
  divider: {
    margin: '32px 0'
  }
}));

const RestoreSource = forwardRef<PageRef>((_, forwardRef) => {
  const [
    restoreContext,
    { moveToPage, setDisableSubmit, setSubmitLabel, setisSubmitting }
  ] = GetRestoreContext();

  const classes = useStyles();

  const validateTables = useMutation((values: ValidateRestoreApiReq) =>
    validateRestorableTables(values)
  );

  const { handleSubmit, setValue, watch } = useFormContext<RestoreFormModel>();

  const { t } = useTranslation('translation', {
    keyPrefix: 'backup.restore.source'
  });

  const sourceKeySpace = watch('source.keyspace');

  useEffect(() => {
    setDisableSubmit(sourceKeySpace === null);
  }, [sourceKeySpace]);

  useMount(() => {
    setSubmitLabel(t('buttonLabels.next', { keyPrefix: 'backup' }));
    setDisableSubmit(false);
  });

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () => {
        handleSubmit((values) => {
          // validate whether the selected PITR time is within the retention period.
          // if not, show an error message and prevent the user from proceeding.
          // the api, returns true always if pitr is not enabled
          const payload = prepareValidationPayload(values, restoreContext);

          setisSubmitting(true);
          setDisableSubmit(true);

          validateTables
            .mutateAsync(payload)
            .then((resp) => {
              if (!resp.data.success) {
                toast.error(resp.data.error);
                setisSubmitting(false);
                setDisableSubmit(false);
                return;
              }
              // save the user selected keyspace if the validation is successful
              setValue('keyspacesToRestore', payload);
              setisSubmitting(false);
              setDisableSubmit(false);
              moveToPage(Page.TARGET);
            })
            .catch((error) => {
              toast.error(createErrorMessage(error));
              setisSubmitting(false);
              setDisableSubmit(false);
            });
        })();
      },
      onPrev: () => { }
    }),
    [restoreContext]
  );

  return (
    <div className={classes.root}>
      <SelectDBAndTables />
      <Divider orientation="horizontal" className={classes.divider} />
      <SelectTimeframe />
    </div>
  );
});

RestoreSource.displayName = 'SelectKeyspaces';

export default RestoreSource;
