/*
 * Created on Thu Feb 16 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, useEffect } from 'react';
import * as Yup from 'yup';
import clsx from 'clsx';
import { useMutation, useQueryClient } from 'react-query';
import { Box, Grid, makeStyles } from '@material-ui/core';
import { FieldArrayPath, useFieldArray, useForm } from 'react-hook-form';
import { Trans, useTranslation } from 'react-i18next';
import { yupResolver } from '@hookform/resolvers/yup';
import { toast } from 'react-toastify';
import { YBButton, YBModal, YBRadio, YBInputField } from '../../../components';
import { useCommonStyles } from './CommonStyles';
import { CustomVariable } from './ICustomVariables';
import { ALERT_TEMPLATES_QUERY_KEY, createCustomAlertTemplteVariable } from './CustomVariablesAPI';
import { isDefinedNotNull } from '../../../../utils/ObjectUtils';
import { createErrorMessage } from '../../universe/universe-form/utils/helpers';
import { Add, Edit } from '@material-ui/icons';
import { ReactComponent as BulbIcon } from '../../../assets/bulb.svg';
import { ReactComponent as Trash } from '../../../assets/trashbin.svg';

type CustomVariableEditorModalProps = {
  open: boolean;
  onHide: Function;
  editValues?: CustomVariable;
};

interface CustomVariablesForm {
  customVariableName: string;
  possibleValues: { text: string; isDefault: boolean }[];
}

const INITIAL_FORM_VALUE: CustomVariablesForm = {
  customVariableName: '',
  possibleValues: [{ text: '', isDefault: true }]
};

const useStyles = makeStyles((theme) => ({
  root: {
    overflow: 'visible',
    '& .MuiInputLabel-root': {
      textTransform: 'none',
      fontSize: '13px',
      color: theme.palette.common.black
    }
  },
  headerIcon: {
    color: theme.palette.orange[500],
    width: '25px',
    height: '25px'
  },
  valuesPanel: {
    marginTop: theme.spacing(0.8),
    padding: theme.spacing(2)
  },

  possibleValuesInputsArea: {
    marginTop: theme.spacing(3)
  },
  possibleValuesInput: {
    marginBottom: theme.spacing(2)
  },
  deleteIcon: {
    cursor: 'pointer',
    height: '18px',
    width: '18px'
  }
}));

export const CustomVariableEditorModal: FC<CustomVariableEditorModalProps> = ({
  open,
  onHide,
  editValues
}) => {
  const { t } = useTranslation();
  const classes = useStyles();
  const commonStyles = useCommonStyles();
  const queryClient = useQueryClient();

  const validationSchema = Yup.object()
    .shape({
      customVariableName: Yup.string()
        .required(t('common.requiredField'))
        .matches(
          /^[a-zA-Z0-9_]*$/,
          t('alertCustomTemplates.customVariables.createNewVariableModal.nameError')
        ),
      possibleValues: Yup.array()
        .min(1, t('common.requiredField'))
        .of(
          Yup.object().shape({
            text: Yup.string().required(t('common.requiredField')),
            isDefault: Yup.boolean()
          })
        )
    })
    .test('possibleValues', t('common.requiredField'), function (values) {
      if (!values?.possibleValues) return true;
      const { possibleValues } = values;
      return possibleValues.length > 0 && possibleValues.some((v: any) => v.isDefault);
    });

  const {
    control,
    getValues,
    handleSubmit,
    setValue,
    formState: { errors, isValid },
    reset
  } = useForm<CustomVariablesForm>({
    mode: 'onChange',
    defaultValues: INITIAL_FORM_VALUE,
    resolver: yupResolver(validationSchema)
  });

  const { fields, append, remove } = useFieldArray<CustomVariablesForm>({
    control,
    name: 'possibleValues'
  });

  const isEditMode = isDefinedNotNull(editValues);

  useEffect(() => {
    if (!open) return;
    if (isEditMode) {
      reset({
        customVariableName: editValues?.name,
        possibleValues: editValues?.possibleValues.map((t) => {
          return { text: t, isDefault: t === editValues.defaultValue };
        })
      });
    } else {
      reset(INITIAL_FORM_VALUE);
    }
  }, [editValues, isEditMode, reset, open]);

  const formValues = getValues();

  const doCreateVariable = useMutation(
    (variables: CustomVariable[]) => {
      return createCustomAlertTemplteVariable(variables);
    },
    {
      onSuccess: (_resp, variables) => {
        onHide();
        reset();
        toast.success(
          <Trans
            i18nKey={`alertCustomTemplates.customVariables.${
              isEditMode ? 'edit' : 'create'
            }Success`}
            values={{ variable_name: variables[0].name }}
            components={{ u: <u /> }}
          />
        );
        queryClient.invalidateQueries(ALERT_TEMPLATES_QUERY_KEY.fetchAlertTemplateVariables);
      },
      onError(error) {
        toast.error(createErrorMessage(error));
      }
    }
  );

  const handleFormSubmit = handleSubmit((formValues) => {
    const variables: CustomVariable[] = [
      {
        name: formValues.customVariableName,
        possibleValues: formValues.possibleValues.map((pv) => pv.text),
        defaultValue: formValues.possibleValues.find((pv) => pv.isDefault)!.text,
        uuid: isEditMode ? editValues?.uuid : undefined
      }
    ];
    doCreateVariable.mutate(variables);
  });

  // if no default value is selected, make the first option as default
  useEffect(() => {
    if (!open || formValues.possibleValues.length === 0) return;
    const isDefaultAvailable = formValues.possibleValues.some((v) => v.isDefault);
    if (!isDefaultAvailable) {
      setValue('possibleValues', [
        ...formValues.possibleValues.map((t, i) => {
          return { ...t, isDefault: i === 0 };
        })
      ]);
    }
  }, [formValues.possibleValues, open]);

  if (!open) return null;

  return (
    <YBModal
      open={open}
      title={
        isEditMode
          ? t('alertCustomTemplates.customVariables.createNewVariableModal.editTitle')
          : t('alertCustomTemplates.customVariables.createNewVariableModal.title')
      }
      titleIcon={
        isEditMode ? (
          <Edit className={classes.headerIcon} />
        ) : (
          <Add className={classes.headerIcon} />
        )
      }
      onSubmit={handleFormSubmit}
      cancelLabel={t('common.cancel')}
      submitLabel={t('common.save')}
      onClose={() => onHide()}
      buttonProps={{
        primary: {
          disabled: !isValid
        }
      }}
      dialogContentProps={{
        className: classes.root,
        dividers: true
      }}
      enableBackdropDismiss
    >
      <YBInputField
        fullWidth
        name="customVariableName"
        control={control}
        label={t('alertCustomTemplates.customVariables.createNewVariableModal.name')}
        data-testid="custom-variable-name-input"
        disabled={isEditMode}
      />
      <Box mt={4}>{t('alertCustomTemplates.customVariables.createNewVariableModal.values')}</Box>

      <Box className={clsx(commonStyles.defaultBorder, classes.valuesPanel)}>
        <Grid container alignItems="center">
          <Grid item xs={1} md={1} lg={1}>
            <BulbIcon />
          </Grid>
          <Grid container item xs={10} md={10} lg={10} alignItems="flex-end">
            <Box className={commonStyles.helpText}>
              <Trans
                i18nKey="alertCustomTemplates.customVariables.createNewVariableModal.valuesHelpText"
                components={{ bold: <b /> }}
              />
            </Box>
          </Grid>
        </Grid>
        <Grid container className={classes.possibleValuesInputsArea}>
          {fields.map((field, index) => {
            return (
              <Grid
                container
                key={field.id}
                alignItems="center"
                spacing={2}
                className={classes.possibleValuesInput}
              >
                <Grid item md={9} xs={9} lg={9}>
                  <YBInputField
                    placeholder={`value ${index + 1}`}
                    fullWidth
                    name={`possibleValues.${index}.text` as FieldArrayPath<string[]>}
                    data-testid={`custom-variable-input-${index}`}
                    control={control}
                  />
                </Grid>
                <Grid item md={2} xs={2} lg={2}>
                  <YBRadio
                    name={`possibleValues.${index}.isDefault`}
                    checked={formValues.possibleValues[index].isDefault}
                    data-testid={`custom-variable-radio-${index}`}
                    onChange={() => {
                      setValue('possibleValues', [
                        ...formValues.possibleValues.map((t, i) => {
                          return { ...t, isDefault: i === index };
                        })
                      ]);
                    }}
                  />
                  {t('alertCustomTemplates.customVariables.createNewVariableModal.default')}
                </Grid>
                <Grid item md={1} xs={1} lg={1}>
                  <Trash
                    className={classes.deleteIcon}
                    data-testid={`custom-variable-delete-${index}`}
                    onClick={() => {
                      remove(index);
                    }}
                  />
                </Grid>
              </Grid>
            );
          })}
        </Grid>
        {(errors.possibleValues as any)?.message && (
          <div className={commonStyles.formErrText}>{(errors.possibleValues as any)?.message}</div>
        )}
        <YBButton
          variant="secondary"
          startIcon={<Add />}
          onClick={() => append({ text: '', isDefault: formValues.possibleValues.length === 0 })}
          data-testid={`custom-variable-add-button`}
        >
          {t('alertCustomTemplates.customVariables.createNewVariableModal.addValuesButton')}
        </YBButton>
      </Box>
    </YBModal>
  );
};
