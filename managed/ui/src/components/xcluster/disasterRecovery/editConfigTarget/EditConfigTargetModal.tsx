import { Typography } from '@material-ui/core';
import { AxiosError } from 'axios';
import { useState } from 'react';
import { FormProvider, SubmitHandler, useForm } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { useMutation, useQueryClient } from 'react-query';
import { browserHistory } from 'react-router';
import { toast } from 'react-toastify';

import { CurrentFormStep } from './CurrentFormStep';
import { StorageConfigOption } from '../../sharedComponents/ReactSelectStorageConfig';
import { Universe } from '../../../../redesign/helpers/dtos';
import { YBErrorIndicator } from '../../../common/indicators';
import { YBButton, YBModal, YBModalProps } from '../../../../redesign/components';
import { api, drConfigQueryKey, EditDrConfigRequest } from '../../../../redesign/helpers/api';
import { assertUnreachableCase, handleServerError } from '../../../../utils/errorHandlingUtils';
import { fetchTaskUntilItCompletes } from '../../../../actions/xClusterReplication';

import { DrConfig } from '../dtos';

import toastStyles from '../../../../redesign/styles/toastStyles.module.scss';

interface EditConfigTargetModalProps {
  drConfig: DrConfig;
  modalProps: YBModalProps;

  redirectUrl?: string;
}

export interface EditConfigTargetFormValues {
  targetUniverse: Universe;
  storageConfig: StorageConfigOption;
}

export const FormStep = {
  SELECT_TARGET_UNIVERSE: 'selectTargetUniverse',
  CONFIGURE_BOOTSTRAP: 'configureBootstrap'
} as const;
export type FormStep = typeof FormStep[keyof typeof FormStep];

const FIRST_FORM_STEP = FormStep.SELECT_TARGET_UNIVERSE;
const MODAL_NAME = 'EditDrConfigModal';
const TRANSLATION_KEY_PREFIX = 'clusterDetail.disasterRecovery.config.editTargetModal';

/**
 * This modal handles changing the DR replica universe
 * of an existing DR config.
 */
export const EditConfigTargetModal = ({
  drConfig,
  modalProps,
  redirectUrl
}: EditConfigTargetModalProps) => {
  const [currentFormStep, setCurrentFormStep] = useState<FormStep>(FIRST_FORM_STEP);
  const queryClient = useQueryClient();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  const formMethods = useForm<EditConfigTargetFormValues>();

  const editDrConfigMutation = useMutation(
    (formValues: EditConfigTargetFormValues) => {
      const editDrConfigRequest: EditDrConfigRequest = {
        newTargetUniverseUuid: formValues.targetUniverse.universeUUID,
        bootstrapBackupParams: {
          storageConfigUUID: formValues.storageConfig.value.uuid
        }
      };
      return api.editDrConfig(drConfig.uuid, editDrConfigRequest);
    },
    {
      onSuccess: (response) => {
        const invalidateQueries = () => {
          queryClient.invalidateQueries(drConfigQueryKey.ALL, { exact: true });
          queryClient.invalidateQueries(drConfigQueryKey.detail(drConfig.uuid));
        };
        const handleTaskCompletion = (error: boolean) => {
          if (error) {
            toast.error(
              <span className={toastStyles.toastMessage}>
                <i className="fa fa-exclamation-circle" />
                <Typography variant="body2" component="span">
                  {t('error.taskFailure')}
                </Typography>
                <a href={`/tasks/${response.taskUUID}`} rel="noopener noreferrer" target="_blank">
                  {t('viewDetails', { keyPrefix: 'task' })}
                </a>
              </span>
            );
          } else {
            toast.success(
              <Typography variant="body2" component="span">
                {t('success.taskSuccess')}
              </Typography>
            );
          }
          invalidateQueries();
        };

        modalProps.onClose();
        if (redirectUrl) {
          browserHistory.push(redirectUrl);
        }
        fetchTaskUntilItCompletes(response.taskUUID, handleTaskCompletion, invalidateQueries);
      },
      onError: (error: Error | AxiosError) =>
        handleServerError(error, { customErrorLabel: t('error.requestFailureLabel') })
    }
  );

  if (drConfig.xClusterConfig.sourceUniverseUUID === undefined) {
    return (
      <YBModal
        title={t('title')}
        cancelLabel={t('cancel', { keyPrefix: 'common' })}
        cancelTestId={`${MODAL_NAME}-CancelButton`}
        {...modalProps}
      >
        <YBErrorIndicator customErrorMessage="The DR primary universe is not defined for this DR configuration." />
      </YBModal>
    );
  }

  const onSubmit: SubmitHandler<EditConfigTargetFormValues> = (formValues) => {
    switch (currentFormStep) {
      case FormStep.SELECT_TARGET_UNIVERSE:
        setCurrentFormStep(FormStep.CONFIGURE_BOOTSTRAP);
        return;
      case FormStep.CONFIGURE_BOOTSTRAP:
        return editDrConfigMutation.mutateAsync(formValues);
      default:
        return assertUnreachableCase(currentFormStep);
    }
  };

  const handleBackNavigation = () => {
    switch (currentFormStep) {
      case FIRST_FORM_STEP:
        return;
      case FormStep.CONFIGURE_BOOTSTRAP:
        setCurrentFormStep(FormStep.SELECT_TARGET_UNIVERSE);
        return;
      default:
        assertUnreachableCase(currentFormStep);
    }
  };
  const getSubmitlabel = () => {
    switch (currentFormStep) {
      case FormStep.SELECT_TARGET_UNIVERSE:
        return t('step.selectTargetUniverse.nextButton');
      case FormStep.CONFIGURE_BOOTSTRAP:
        return t('applyChanges', { keyPrefix: 'common' });
      default:
        return assertUnreachableCase(currentFormStep);
    }
  };

  const submitLabel = getSubmitlabel();
  const isFormDisabled = formMethods.formState.isSubmitting;
  return (
    <YBModal
      title={t('title')}
      submitLabel={submitLabel}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
      buttonProps={{ primary: { disabled: isFormDisabled } }}
      isSubmitting={formMethods.formState.isSubmitting}
      onSubmit={formMethods.handleSubmit(onSubmit)}
      submitTestId={`${MODAL_NAME}-SubmitButton`}
      cancelTestId={`${MODAL_NAME}-CancelButton`}
      footerAccessory={
        currentFormStep !== FIRST_FORM_STEP && (
          <YBButton variant="secondary" onClick={handleBackNavigation}>
            {t('back', { keyPrefix: 'common' })}
          </YBButton>
        )
      }
      {...modalProps}
    >
      <FormProvider {...formMethods}>
        <CurrentFormStep
          currentFormStep={currentFormStep}
          sourceUniverseUUID={drConfig.xClusterConfig.sourceUniverseUUID}
          isFormDisabled={isFormDisabled}
        />
      </FormProvider>
    </YBModal>
  );
};
