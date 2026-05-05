import { useState } from 'react';
import { AxiosError } from 'axios';
import { makeStyles } from '@material-ui/core';
import { FormProvider, SubmitHandler, useForm } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { useMutation, useQuery } from 'react-query';

import { YBLoadingCircleIcon } from '@app/components/common/indicators';
import { YBButton, YBModal, type YBModalProps } from '@app/redesign/components';
import { YBStepper } from '@app/redesign/components/YBStepper/YBStepper';
import { ApiPermissionMap } from '@app/redesign/features/rbac/ApiAndUserPermMapping';
import { hasNecessaryPerm } from '@app/redesign/features/rbac/common/RbacApiPermValidator';
import { RBAC_ERR_MSG_NO_PERM } from '@app/redesign/features/rbac/common/validator/ValidatorUtils';
import {
  api,
  dbReleaseQueryKey,
  runtimeConfigQueryKey,
  universeQueryKey
} from '@app/redesign/helpers/api';
import { RuntimeConfigKey } from '@app/redesign/helpers/constants';
import { assertUnreachableCase, handleServerError } from '@app/utils/errorHandlingUtils';
import { getUniverse, startSoftwareUpgrade } from '@app/v2/api/universe/universe';
import type { UniverseSoftwareUpgradeReqBody } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { CurrentDbUpgradeFormStep } from './CurrentDbUpgradeFormStep';
import { DbUpgradeModalContextProvider } from './DbUpgradeModalContext';
import {
  DB_UPGRADE_FIRST_FORM_STEP,
  DbUpgradeFormStep,
  UpgradeMethod,
  UpgradePace
} from './constants';
import { ReleaseState, type YbdbRelease } from './dtos';
import type { DBUpgradeFormFields } from './types';
import {
  buildCanaryUpgradeConfig,
  buildRequestPayload,
  getDefaultCanaryUpgradeConfig
} from './utils/formUtils';
import { buildVersionOptions } from './utils/versionUtils';
import { DbUpgradeSummaryCard } from './upgrade-summary/DbUpgradeSummaryCard';
import {
  useRefreshSoftwareUpgradeTasksCache,
  useRefreshUniverseDetailsCache
} from '@app/redesign/helpers/cacheUtils';

const MODAL_NAME = 'DbUpgradeModal';
const TRANSLATION_KEY_PREFIX = 'universeActions.dbUpgrade.upgradeModal';

const useStyles = makeStyles((theme) => ({
  modalContainer: {
    display: 'flex',

    height: '100%',
    minHeight: 0,
    padding: 0
  },

  formScrollArea: {
    display: 'flex',
    flexDirection: 'column',

    overflowY: 'auto'
  },

  infoPanel: {
    flexShrink: 0,

    width: 282,
    height: '100%',
    padding: theme.spacing(2),

    overflowY: 'auto',
    backgroundColor: theme.palette.ybacolors.grey050,
    borderLeft: `1px solid ${theme.palette.grey[200]}`
  },
  loadingContainer: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(2),
    justifyContent: 'center',
    alignItems: 'center'
  },
  leftPanel: {
    display: 'flex',
    flexDirection: 'column',
    flex: 1
  },
  stepperContainer: {
    padding: theme.spacing(2, 3),
    borderBottom: `1px solid ${theme.palette.grey[200]}`
  }
}));

interface DBUpgradeModalProps {
  universeUuid: string;
  modalProps: YBModalProps;
}

export const DbUpgradeModal = ({
  universeUuid: currentUniverseUuid,
  modalProps
}: DBUpgradeModalProps) => {
  const [currentFormStep, setCurrentFormStep] = useState<DbUpgradeFormStep>(
    DbUpgradeFormStep.DB_VERSION
  );
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = useStyles();
  const refreshUniverseDetailsCache = useRefreshUniverseDetailsCache(currentUniverseUuid);
  const refreshSoftwareUpgradeTasksCache = useRefreshSoftwareUpgradeTasksCache(currentUniverseUuid);
  const universeDetailsQuery = useQuery(universeQueryKey.detailsV2(currentUniverseUuid), () =>
    getUniverse(currentUniverseUuid)
  );

  const clusters = universeDetailsQuery.data?.spec?.clusters ?? [];
  const currentDbVersion = universeDetailsQuery.data?.spec?.yb_software_version ?? '';
  const currentReleaseArchitecture = universeDetailsQuery.data?.info?.arch;
  const maxNodesPerBatchMaximum =
    universeDetailsQuery.data?.info?.roll_max_batch_size?.primary_batch_size ?? 1;

  const universeRuntimeConfigsQuery = useQuery(
    runtimeConfigQueryKey.universeScope(currentUniverseUuid),
    () => api.fetchRuntimeConfigs(currentUniverseUuid),
    {
      enabled: !!currentUniverseUuid
    }
  );

  const dbReleasesQuery = useQuery(dbReleaseQueryKey.list(), () => api.getDbReleases(), {
    select: (releases): YbdbRelease[] =>
      (releases ?? []).filter((release) => release.state === ReleaseState.ACTIVE)
  });

  const shouldSkipVersionChecks =
    universeRuntimeConfigsQuery.data?.configEntries?.find(
      (c: any) => c.key === RuntimeConfigKey.SKIP_VERSION_CHECKS
    )?.value === 'true';
  const releasesList = dbReleasesQuery.data ?? [];
  const targetReleaseOptions = buildVersionOptions(
    releasesList,
    currentDbVersion,
    currentReleaseArchitecture,
    shouldSkipVersionChecks
  );

  const formMethods = useForm<DBUpgradeFormFields>({
    defaultValues: {
      targetDbVersion: '',
      upgradeMethod: UpgradeMethod.EXPRESS,
      upgradePace: UpgradePace.ROLLING,
      maxNodesPerBatch: 1,
      waitBetweenBatchesSeconds: 180
    },
    mode: 'onChange'
  });

  const hasRequiredUpgradePermission = hasNecessaryPerm({
    onResource: currentUniverseUuid,
    ...ApiPermissionMap.UPGRADE_NEW_UNIVERSE_SOFTWARE
  });

  const upgradeSoftware = useMutation(
    (data: UniverseSoftwareUpgradeReqBody) => startSoftwareUpgrade(currentUniverseUuid, data),
    {
      onSuccess: () => {
        refreshSoftwareUpgradeTasksCache();
        modalProps.onClose();
      },
      onError: (error: Error | AxiosError) =>
        handleServerError(error, { customErrorLabel: t('toast.dbUpgradeFailed') })
    }
  );

  const submitExpressUpgrade = async (values: DBUpgradeFormFields) => {
    const requestPayload = buildRequestPayload(values);
    await upgradeSoftware.mutateAsync(requestPayload);
  };
  const submitCanaryUpgrade = async (values: DBUpgradeFormFields) => {
    const requestPayload: UniverseSoftwareUpgradeReqBody = {
      ...buildRequestPayload(values),
      canary_upgrade_config: buildCanaryUpgradeConfig(values)
    };
    await upgradeSoftware.mutateAsync(requestPayload);
  };

  const handleModalPrimaryButtonClick: SubmitHandler<DBUpgradeFormFields> = async (values) => {
    switch (currentFormStep) {
      case DbUpgradeFormStep.DB_VERSION:
        setCurrentFormStep(DbUpgradeFormStep.UPGRADE_METHOD);
        return;
      case DbUpgradeFormStep.UPGRADE_METHOD:
        if (values.upgradeMethod === UpgradeMethod.EXPRESS) {
          return await submitExpressUpgrade(values);
        }
        formMethods.reset({
          ...values,
          canaryUpgradeConfig: getDefaultCanaryUpgradeConfig(clusters)
        });
        setCurrentFormStep(DbUpgradeFormStep.UPGRADE_PLAN);
        return;
      case DbUpgradeFormStep.UPGRADE_PLAN:
        setCurrentFormStep(DbUpgradeFormStep.UPGRADE_PACE);
        return;
      case DbUpgradeFormStep.UPGRADE_PACE:
        return await submitCanaryUpgrade(values);
      default:
        return assertUnreachableCase(currentFormStep);
    }
  };

  const handleBackNavigation = () => {
    // We can clear errors here because prior steps have already been validated
    // and future steps will be revalidated when the user clicks the next page button.
    formMethods.clearErrors();

    switch (currentFormStep) {
      case DbUpgradeFormStep.DB_VERSION:
        return;
      case DbUpgradeFormStep.UPGRADE_METHOD:
        setCurrentFormStep(DbUpgradeFormStep.DB_VERSION);
        return;
      case DbUpgradeFormStep.UPGRADE_PLAN:
        setCurrentFormStep(DbUpgradeFormStep.UPGRADE_METHOD);
        return;
      case DbUpgradeFormStep.UPGRADE_PACE:
        setCurrentFormStep(DbUpgradeFormStep.UPGRADE_PLAN);
        return;
      default:
        return assertUnreachableCase(currentFormStep);
    }
  };

  const getSubmitLabel = (): string => {
    switch (currentFormStep) {
      case DbUpgradeFormStep.DB_VERSION:
        return t('submitLabel.next');
      case DbUpgradeFormStep.UPGRADE_METHOD:
        return formMethods.watch('upgradeMethod') === UpgradeMethod.EXPRESS
          ? t('submitLabel.upgrade')
          : t('submitLabel.next');
      case DbUpgradeFormStep.UPGRADE_PLAN:
        return t('submitLabel.next');
      case DbUpgradeFormStep.UPGRADE_PACE:
        return t('submitLabel.upgrade');
      default:
        return assertUnreachableCase(currentFormStep);
    }
  };

  const targetDbVersion = formMethods.watch('targetDbVersion');
  const getIsNextStepDisabled = (): boolean => {
    switch (currentFormStep) {
      case DbUpgradeFormStep.DB_VERSION:
        return targetDbVersion === '';
      case DbUpgradeFormStep.UPGRADE_METHOD:
      case DbUpgradeFormStep.UPGRADE_PLAN:
      case DbUpgradeFormStep.UPGRADE_PACE:
        return false;
      default:
        return assertUnreachableCase(currentFormStep);
    }
  };

  const upgradeMethod = formMethods.watch('upgradeMethod');
  const formSteps =
    upgradeMethod === UpgradeMethod.EXPRESS
      ? [DbUpgradeFormStep.DB_VERSION, DbUpgradeFormStep.UPGRADE_METHOD]
      : [
          DbUpgradeFormStep.DB_VERSION,
          DbUpgradeFormStep.UPGRADE_METHOD,
          DbUpgradeFormStep.UPGRADE_PLAN,
          DbUpgradeFormStep.UPGRADE_PACE
        ];
  const formStepperLabels: Record<string, string> =
    upgradeMethod === UpgradeMethod.EXPRESS
      ? {
          [DbUpgradeFormStep.DB_VERSION]: t('stepper.dbVersion'),
          [DbUpgradeFormStep.UPGRADE_METHOD]: t('stepper.upgradeMethod')
        }
      : {
          [DbUpgradeFormStep.DB_VERSION]: t('stepper.dbVersion'),
          [DbUpgradeFormStep.UPGRADE_METHOD]: t('stepper.upgradeMethod'),
          [DbUpgradeFormStep.UPGRADE_PLAN]: t('stepper.upgradePlan'),
          [DbUpgradeFormStep.UPGRADE_PACE]: t('stepper.upgradePace')
        };

  const modalTitle = t('modalTitle');
  const submitLabel = getSubmitLabel();
  const cancelLabel = t('cancel', { keyPrefix: 'common' });
  const isFormDisabled = formMethods.formState.isSubmitting || !hasRequiredUpgradePermission;
  return (
    <YBModal
      title={modalTitle}
      submitLabel={submitLabel}
      cancelLabel={cancelLabel}
      overrideWidth="1100px"
      overrideHeight="820px"
      submitTestId={`${MODAL_NAME}-SubmitButton`}
      cancelTestId={`${MODAL_NAME}-CancelButton`}
      size="xl"
      dialogContentProps={{ className: classes.modalContainer }}
      onSubmit={formMethods.handleSubmit(handleModalPrimaryButtonClick)}
      isSubmitting={formMethods.formState.isSubmitting}
      hideCloseBtn={false}
      titleSeparator
      footerAccessory={
        currentFormStep !== DB_UPGRADE_FIRST_FORM_STEP && (
          <YBButton variant="secondary" onClick={handleBackNavigation}>
            {t('back', { keyPrefix: 'common' })}
          </YBButton>
        )
      }
      buttonProps={{
        primary: {
          disabled: isFormDisabled || getIsNextStepDisabled()
        }
      }}
      submitButtonTooltip={!hasRequiredUpgradePermission ? RBAC_ERR_MSG_NO_PERM : ''}
      {...modalProps}
    >
      <FormProvider {...formMethods}>
        <div className={classes.leftPanel}>
          <div className={classes.stepperContainer}>
            <YBStepper steps={formStepperLabels} currentStep={currentFormStep} />
          </div>
          <div className={classes.formScrollArea}>
            {universeDetailsQuery.isLoading ||
            universeRuntimeConfigsQuery.isLoading ||
            dbReleasesQuery.isLoading ||
            !universeDetailsQuery.data ? (
              <div className={classes.loadingContainer}>
                <YBLoadingCircleIcon />
              </div>
            ) : (
              <DbUpgradeModalContextProvider
                value={{
                  currentUniverseUuid,
                  universeDetails: universeDetailsQuery.data,
                  currentDbVersion,
                  clusters,
                  maxNodesPerBatchMaximum,
                  targetReleaseOptions,
                  closeModal: modalProps.onClose
                }}
              >
                <CurrentDbUpgradeFormStep currentFormStep={currentFormStep} />
              </DbUpgradeModalContextProvider>
            )}
          </div>
        </div>
        <div className={classes.infoPanel}>
          <DbUpgradeSummaryCard currentFormStep={currentFormStep} formSteps={formSteps} />
        </div>
      </FormProvider>
    </YBModal>
  );
};
