import { useState } from 'react';
import { useMutation, useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { SubmitHandler, useForm, FormProvider } from 'react-hook-form';
import { makeStyles } from '@material-ui/core';
import { AxiosError } from 'axios';

import { YBButton, YBModal, type YBModalProps } from '../../../../components';
import {
  api,
  dbReleaseQueryKey,
  runtimeConfigQueryKey,
  universeQueryKey
} from '../../../../helpers/api';
import { getUniverse, startSoftwareUpgrade } from '../../../../../v2/api/universe/universe';
import type { UniverseSoftwareUpgradeReqBody } from '../../../../../v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { YBLoadingCircleIcon } from '../../../../../components/common/indicators';
import { RuntimeConfigKey } from '../../../../helpers/constants';
import { assertUnreachableCase, handleServerError } from '../../../../../utils/errorHandlingUtils';
import { hasNecessaryPerm } from '../../../rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../rbac/ApiAndUserPermMapping';
import { RBAC_ERR_MSG_NO_PERM } from '../../../rbac/common/validator/ValidatorUtils';
import { ReleaseState, type YbdbRelease } from './dtos';
import { buildVersionOptions } from './utils/versionUtils';
import { DbUpgradeSummaryCard } from './DbUpgradeSummaryCard';
import { CurrentDbUpgradeFormStep } from './CurrentDbUpgradeFormStep';
import {
  DB_UPGRADE_FIRST_FORM_STEP,
  DbUpgradeFormStep,
  UpgradeMethod,
  UpgradePace
} from './constants';
import type { DBUpgradeFormFields } from './types';
import {
  buildCanaryUpgradeConfig,
  buildRequestPayload,
  getDefaultCanaryUpgradeConfig
} from './utils/formUtils';

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

    flex: 1,
    minHeight: 0,
    overflowY: 'auto'
  },

  infoPanel: {
    flexShrink: 0,

    width: 282,
    height: '100%',
    padding: theme.spacing(2),

    backgroundColor: '#F7F9FB',
    borderLeft: `1px solid ${theme.palette.grey[200]}`
  },
  loadingContainer: {
    flex: 1,
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(2),
    justifyContent: 'center',
    alignItems: 'center'
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
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = useStyles();
  const [currentFormStep, setCurrentFormStep] = useState<DbUpgradeFormStep>(
    DbUpgradeFormStep.DB_VERSION
  );

  const universeDetailsQuery = useQuery(universeQueryKey.detailsV2(currentUniverseUuid), () =>
    getUniverse(currentUniverseUuid)
  );

  const clusters = universeDetailsQuery.data?.spec?.clusters ?? [];
  const currentDbVersion = universeDetailsQuery.data?.spec?.yb_software_version ?? '';
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
  const targetReleaseOptions = buildVersionOptions(releasesList, currentDbVersion, shouldSkipVersionChecks);

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

  const modalTitle = t('modalTitle');
  const submitLabel = getSubmitLabel();
  const cancelLabel = t('cancel', { keyPrefix: 'common' });

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
          disabled: !hasRequiredUpgradePermission || !targetDbVersion
        }
      }}
      submitButtonTooltip={!hasRequiredUpgradePermission ? RBAC_ERR_MSG_NO_PERM : ''}
      {...modalProps}
    >
      <div className={classes.formScrollArea}>
        <FormProvider {...formMethods}>
          {universeDetailsQuery.isLoading ||
          universeRuntimeConfigsQuery.isLoading ||
          dbReleasesQuery.isLoading ? (
            <div className={classes.loadingContainer}>
              <YBLoadingCircleIcon />
            </div>
          ) : (
            <CurrentDbUpgradeFormStep
              currentUniverseUuid={currentUniverseUuid}
              currentFormStep={currentFormStep}
              currentRelease={currentDbVersion}
              targetReleaseOptions={targetReleaseOptions}
              maxNodesPerBatchMaximum={maxNodesPerBatchMaximum}
              onPreCheckSuccess={modalProps.onClose}
            />
          )}
        </FormProvider>
      </div>
      <div className={classes.infoPanel}>
        <DbUpgradeSummaryCard />
      </div>
    </YBModal>
  );
};
