import { useState } from 'react';
import _ from 'lodash';
import { toast } from 'react-toastify';
import { useMutation, useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { SubmitHandler, useForm, FormProvider } from 'react-hook-form';
import { makeStyles, Typography } from '@material-ui/core';
import { AxiosError } from 'axios';

import { YBButton, YBModal, type YBModalProps } from '../../../../components';
import { api, dbReleaseQueryKey, runtimeConfigQueryKey } from '../../../../helpers/api';
import { startSoftwareUpgrade } from '../../../../../v2/api/universe/universe';
import type { UniverseSoftwareUpgradeReqBody } from '../../../../../v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { getPrimaryCluster } from '../../universe-form/utils/helpers';
import { Universe } from '../../universe-form/utils/dto';
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

const MODAL_NAME = 'DbUpgradeModal';
const TRANSLATION_KEY_PREFIX = 'universeActions.dbUpgrade.upgradeModal';

const useStyles = makeStyles((theme) => ({
  modalContainer: {
    display: 'flex',

    padding: 0
  },

  infoPanel: {
    flexShrink: 0,

    width: 282,
    height: '100%',
    padding: theme.spacing(2),

    backgroundColor: '#F7F9FB',
    borderLeft: `1px solid ${theme.palette.grey[200]}`
  }
}));

interface DBUpgradeModalProps {
  universeData: Universe;
  modalProps: YBModalProps;
}

export const DbUpgradeModal = ({ universeData, modalProps }: DBUpgradeModalProps) => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = useStyles();
  const [currentFormStep, setCurrentFormStep] = useState<DbUpgradeFormStep>(
    DbUpgradeFormStep.DB_VERSION
  );

  const { universeDetails, universeUUID: currentUniverseUuid, rollMaxBatchSize } = universeData;
  const primaryCluster = _.cloneDeep(getPrimaryCluster(universeDetails));
  const currentRelease = primaryCluster?.userIntent.ybSoftwareVersion ?? '';

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
  const targetReleaseOptions = buildVersionOptions(releasesList, currentRelease, shouldSkipVersionChecks);

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
  const selectedVersion = formMethods.watch('targetDbVersion');

  const hasRequiredUpgradePermission = hasNecessaryPerm({
    onResource: currentUniverseUuid,
    ...ApiPermissionMap.UPGRADE_NEW_UNIVERSE_SOFTWARE
  });

  const upgradeSoftware = useMutation(
    (data: UniverseSoftwareUpgradeReqBody) => startSoftwareUpgrade(currentUniverseUuid, data),
    {
      onSuccess: () => {
        toast.success(
          <Typography variant="body2" component="span">
            {t('toast.dbUpgradeInitiated')}
          </Typography>
        );
        modalProps.onClose();
      },
      onError: (error: Error | AxiosError) =>
        handleServerError(error, { customErrorLabel: t('toast.dbUpgradeFailed') })
    }
  );

  const submitExpressUpgrade = async (values: DBUpgradeFormFields) => {
    const data: UniverseSoftwareUpgradeReqBody = {
      version: values.targetDbVersion,
      allow_rollback: true,
      rolling_upgrade: values.upgradePace === UpgradePace.ROLLING,
      ...(values.upgradePace === UpgradePace.ROLLING && {
        roll_max_batch_size: {
          primary_batch_size: values.maxNodesPerBatch,
          read_replica_batch_size: values.maxNodesPerBatch
        },
        sleep_after_master_restart_millis: values.waitBetweenBatchesSeconds
          ? values.waitBetweenBatchesSeconds * 1000
          : undefined,
        sleep_after_tserver_restart_millis: values.waitBetweenBatchesSeconds
          ? values.waitBetweenBatchesSeconds * 1000
          : undefined
      })
    };
    await upgradeSoftware.mutateAsync(data);
  };
  const submitCanaryUpgrade = async (values: DBUpgradeFormFields) => {
    // TODO: Implement canary upgrade
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

  if (universeRuntimeConfigsQuery.isLoading || dbReleasesQuery.isLoading) {
    return <YBLoadingCircleIcon />;
  }

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

  const modalTitle = t('modalTitle');
  const submitLabel = getSubmitLabel();
  const cancelLabel = t('cancel', { keyPrefix: 'common' });

  return (
    <YBModal
      title={modalTitle}
      submitLabel={submitLabel}
      cancelLabel={cancelLabel}
      overrideWidth="1100px"
      overrideHeight="800px"
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
          disabled: !hasRequiredUpgradePermission || !selectedVersion
        }
      }}
      submitButtonTooltip={!hasRequiredUpgradePermission ? RBAC_ERR_MSG_NO_PERM : ''}
      {...modalProps}
    >
      <FormProvider {...formMethods}>
        <CurrentDbUpgradeFormStep
          currentUniverseUuid={currentUniverseUuid}
          currentFormStep={currentFormStep}
          currentRelease={currentRelease}
          targetReleaseOptions={targetReleaseOptions}
          primaryBatchSize={rollMaxBatchSize?.primaryBatchSize ?? 1}
        />
      </FormProvider>
      <div className={classes.infoPanel}>
        <DbUpgradeSummaryCard />
      </div>
    </YBModal>
  );
};
