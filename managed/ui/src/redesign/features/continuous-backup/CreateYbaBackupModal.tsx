import React from 'react';
import { useTranslation } from 'react-i18next';
import { makeStyles, Typography, useTheme } from '@material-ui/core';
import { SubmitHandler, useForm, useController } from 'react-hook-form';
import { toast } from 'react-toastify';
import { AxiosError } from 'axios';

import { YBCheckbox, YBInputField, YBModal, YBModalProps } from '@app/redesign/components';
import { useCreateYbaBackup } from '@app/v2/api/isolated-backup/isolated-backup';
import { YbaComponent } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { handleServerError } from '@app/utils/errorHandlingUtils';
import { fetchTaskUntilItCompletes } from '@app/actions/xClusterReplication';

import toastStyles from '../../../redesign/styles/toastStyles.module.scss';

interface CreateYbaBackupModalProps {
  modalProps: YBModalProps;
}

interface CreateYbaBackupFormValues {
  localDirectory: string;
  components: YbaComponent[];
}

const useStyles = makeStyles((theme) => ({
  ybaComponentContainer: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(3),

    marginTop: theme.spacing(2)
  },
  ybaComponent: {
    display: 'flex',
    justifyContent: 'space-between',
    alignItems: 'center',

    padding: theme.spacing(1.5, 2),
    borderRadius: theme.shape.borderRadius,
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,

    '& .MuiCheckbox-root': {
      padding: theme.spacing(0),
      marginRight: theme.spacing(1)
    }
  },
  localDirectoryField: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(1),

    marginTop: theme.spacing(5)
  }
}));

const MODAL_NAME = 'CreateYbaBackupModal';
const TRANSLATION_KEY_PREFIX = 'continuousBackup.createYbaBackupModal';

const YBA_COMPONENT_OPTIONS: YbaComponent[] = ['YBA', 'RELEASES', 'PROMETHEUS'];
const YBA_COMPONENT_I18N_KEY = {
  [YbaComponent.YBA]: 'platformMetadata',
  [YbaComponent.RELEASES]: 'yugabyteDbReleases',
  [YbaComponent.PROMETHEUS]: 'universeMetrics'
};

export const CreateYbaBackupModal = ({ modalProps }: CreateYbaBackupModalProps) => {
  const createYbaBackup = useCreateYbaBackup();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = useStyles();
  const theme = useTheme();
  const formMethods = useForm<CreateYbaBackupFormValues>({
    defaultValues: {
      localDirectory: '',
      components: []
    }
  });

  // Controller for managing components array
  const { field: componentsField } = useController({
    name: 'components',
    control: formMethods.control,
    defaultValue: []
  });

  const handleComponentToggle = (component: YbaComponent, checked: boolean) => {
    const currentComponents = componentsField.value || [];
    const updatedComponents = checked
      ? [...currentComponents, component]
      : currentComponents.filter((c) => c !== component);

    componentsField.onChange(updatedComponents);
  };

  const onSubmit: SubmitHandler<CreateYbaBackupFormValues> = async (formValues) => {
    try {
      await createYbaBackup.mutateAsync(
        {
          data: {
            local_dir: formValues.localDirectory,
            components: formValues.components
          }
        },
        {
          onSuccess: (response) => {
            const handleTaskCompletion = (error: boolean) => {
              if (error) {
                toast.error(
                  <span className={toastStyles.toastMessage}>
                    <i className="fa fa-exclamation-circle" />
                    <Typography variant="body2" component="span">
                      {t('toast.createBackupTaskFailed')}
                    </Typography>
                    <a
                      href={`/tasks/${response.task_uuid}`}
                      rel="noopener noreferrer"
                      target="_blank"
                    >
                      {t('viewDetails', { keyPrefix: 'task' })}
                    </a>
                  </span>
                );
              } else {
                toast.success(
                  <Typography variant="body2">{t('toast.createBackupTaskSuccess')}</Typography>
                );
              }
            };
            toast.success(
              <Typography variant="body2">{t('toast.createBackupRequestSuccess')}</Typography>
            );
            modalProps.onClose();
            fetchTaskUntilItCompletes(response.task_uuid ?? '', handleTaskCompletion);
          },
          onError: (error: Error | AxiosError) => {
            handleServerError(error, {
              customErrorLabel: t('toast.createBackupRequestFailedLabel')
            });
          }
        }
      );
    } catch (error) {
      // Already handled by the onError callback
    }
  };

  const isFormDisabled = formMethods.formState.isSubmitting || createYbaBackup.isLoading;
  const selectedComponents = componentsField.value || [];

  return (
    <YBModal
      title={t('title')}
      submitLabel={t('submitButton')}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
      onSubmit={formMethods.handleSubmit(onSubmit)}
      overrideHeight="fit-content"
      buttonProps={{ primary: { disabled: isFormDisabled } }}
      isSubmitting={createYbaBackup.isLoading}
      size="sm"
      submitTestId={`${MODAL_NAME}-SubmitButton`}
      cancelTestId={`${MODAL_NAME}-CancelButton`}
      {...modalProps}
    >
      <Typography variant="body2">{t('instructions')}</Typography>
      <div className={classes.ybaComponentContainer}>
        {YBA_COMPONENT_OPTIONS.map((component) => (
          <div key={component} className={classes.ybaComponent}>
            <YBCheckbox
              label={t(`component.${YBA_COMPONENT_I18N_KEY[component]}`)}
              checked={selectedComponents.includes(component)}
              onChange={(event: React.ChangeEvent<HTMLInputElement>) =>
                handleComponentToggle(component, event.target.checked)
              }
              disabled={isFormDisabled}
            />
          </div>
        ))}
      </div>
      <div className={classes.localDirectoryField}>
        <Typography variant="body1">{t('exportDestination')}</Typography>
        <YBInputField
          control={formMethods.control}
          name="localDirectory"
          placeholder={t('exportDestinationPlaceholder')}
          disabled={isFormDisabled}
          fullWidth
        />
      </div>
    </YBModal>
  );
};
