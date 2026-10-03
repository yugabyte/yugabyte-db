// Copyright (c) YugabyteDB, Inc.

import { useState } from 'react';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { toast } from 'react-toastify';

import { FirstStep } from './FirstStep/FirstStep';
import {
  DEFAULT_PROMETHEUS_METRICS_PARAMS,
  DEFAULT_UNIVERSE_LOGS_PARAMS,
  SecondStep,
  updateOptions
} from './SecondStep/SecondStep';
import { ThirdStep } from './ThirdStep/ThirdStep';
import { PerfAdvisorAPI, QUERY_KEY } from '@app/redesign/features/PerfAdvisor/api';
import { supportBundleQueryKey } from '@app/redesign/helpers/api';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import { handleServerError } from '../../../utils/errorHandlingUtils';
import { RBAC_ERR_MSG_NO_PERM } from '../../../redesign/features/rbac/common/validator/ValidatorUtils';
import { fetchGlobalRunTimeConfigs } from '../../../api/admin';
import {
  SUPPORT_BUNDLE_PAGE_SIZE,
  createSupportBundle as createSupportBundleRequest,
  deleteSupportBundle as deleteSupportBundleRequest,
  downloadSupportBundle as downloadSupportBundleRequest,
  estimateSupportBundleSize as estimateSupportBundleSizeRequest,
  isSupportBundleUiV2Enabled,
  listSupportBundles
} from '../../../redesign/features/universe/universe-actions/support-bundle';
import { filterTypes } from '../../metrics/MetricsComparisonModal/ComparisonFilterContextProvider';
import { getIsKubernetesUniverse } from '../../../utils/UniverseUtils';
import { getUniverseStatus } from '../helpers/universeHelpers';
import { YBModal } from '../../../redesign/components';

import 'react-bootstrap-table/css/react-bootstrap-table.css';
import './UniverseSupportBundleModal.scss';

const SupportBundleStep = {
  EMPTY: 'empty',
  CREATE_FORM: 'createForm',
  LIST: 'list'
};

const POLLING_INTERVAL = 10000; // ten seconds

const isPerfAdvisorNotFound = (error) =>
  error?.request?.status === 404 || error?.response?.status === 404;

export const UniverseSupportBundleModal = (props) => {
  const { currentUniverse, closeModal } = props;

  const globalRuntimeConfigsQuery = useQuery(
    ['globalRuntimeConfigs'],
    () => fetchGlobalRunTimeConfigs(true).then((res) => res.data),
    { refetchOnMount: 'always' }
  );

  const modalProps = {
    className: 'universe-support-bundle',
    title: 'Support Bundle',
    open: true,
    onClose: closeModal,
    overrideHeight: 'fit-content',
    cancelLabel: 'Close'
  };

  if (!globalRuntimeConfigsQuery.isFetchedAfterMount) {
    return (
      <YBModal {...modalProps} buttonProps={{ primary: { disabled: true } }}>
        <YBLoading />
      </YBModal>
    );
  }

  if (globalRuntimeConfigsQuery.isError || !globalRuntimeConfigsQuery.data) {
    return (
      <YBModal {...modalProps} buttonProps={{ primary: { disabled: true } }}>
        <YBErrorIndicator customErrorMessage="Failed to fetch global runtime configurations." />
      </YBModal>
    );
  }

  const useV2Api = isSupportBundleUiV2Enabled(globalRuntimeConfigsQuery.data?.configEntries);

  return (
    <UniverseSupportBundleWizard
      currentUniverse={currentUniverse}
      closeModal={closeModal}
      useV2Api={useV2Api}
    />
  );
};

const UniverseSupportBundleWizard = (props) => {
  const {
    currentUniverse,
    currentUniverse: { universeDetails },
    closeModal,
    useV2Api
  } = props;

  const paRegistrationQuery = useQuery(
    QUERY_KEY.fetchUniverseRegistrationDetails,
    () => PerfAdvisorAPI.fetchUniverseRegistrationDetails(universeDetails.universeUUID),
    {
      refetchOnMount: 'always',
      retry: (failureCount, error) => {
        if (isPerfAdvisorNotFound(error)) {
          return false;
        }
        return failureCount < 3;
      }
    }
  );
  const isPerfAdvisorRegistered =
    paRegistrationQuery.isSuccess && !!paRegistrationQuery.data?.success;
  const [isOnCreateForm, setIsOnCreateForm] = useState(false);
  const [activePage, setActivePage] = useState(1);
  const defaultOptions = updateOptions(
    filterTypes[0],
    [true, true, true, true, true, true, true, true, true, true, true],
    () => {},
    DEFAULT_UNIVERSE_LOGS_PARAMS,
    {},
    DEFAULT_PROMETHEUS_METRICS_PARAMS
  );
  const [payload, setPayload] = useState(defaultOptions);
  const isK8sUniverse = getIsKubernetesUniverse(currentUniverse);
  const queryClient = useQueryClient();

  const offset = useV2Api ? (activePage - 1) * SUPPORT_BUNDLE_PAGE_SIZE : 0;

  const supportBundlesListQuery = useQuery(
    supportBundleQueryKey.list(universeDetails.universeUUID, useV2Api, activePage),
    () => listSupportBundles(universeDetails.universeUUID, useV2Api, offset),
    {
      keepPreviousData: true,
      refetchOnMount: 'always',
      refetchInterval: (data) => {
        const bundles = data?.bundles ?? [];
        return bundles.some((supportBundle) => supportBundle.status === 'Running')
          ? POLLING_INTERVAL
          : false;
      },
      onSuccess: ({ totalCount: nextTotalCount }) => {
        if (useV2Api) {
          const lastPage = Math.max(1, Math.ceil(nextTotalCount / SUPPORT_BUNDLE_PAGE_SIZE));
          if (activePage > lastPage) {
            setActivePage(lastPage);
          }
        }
      },
      onError: (error) => {
        const cachedLists = queryClient.getQueriesData([
          ...supportBundleQueryKey.ALL,
          'list',
          universeDetails.universeUUID
        ]);
        if (cachedLists.some(([, cachedList]) => cachedList)) {
          handleServerError(error, { customErrorLabel: 'Failed to fetch support bundles' });
        }
      }
    }
  );

  const supportBundles = supportBundlesListQuery.data?.bundles ?? [];
  const totalCount = supportBundlesListQuery.data?.totalCount ?? 0;
  const currentStep = isOnCreateForm
    ? SupportBundleStep.CREATE_FORM
    : totalCount === 0
      ? SupportBundleStep.EMPTY
      : SupportBundleStep.LIST;

  const createSupportBundleMutation = useMutation(
    (supportBundlePayload) =>
      createSupportBundleRequest(universeDetails.universeUUID, supportBundlePayload, useV2Api),
    {
      onSuccess: async () => {
        setActivePage(1);
        await queryClient.invalidateQueries(supportBundleQueryKey.ALL);
        setIsOnCreateForm(false);
        setPayload(defaultOptions);
      },
      onError: (error) => {
        if (error?.response?.status === 403) {
          toast.error(RBAC_ERR_MSG_NO_PERM, { autoClose: 3000 });
          return;
        }
        handleServerError(error, { customErrorLabel: 'Failed to create support bundle' });
      }
    }
  );

  const handleDeleteBundle = async (universeUUID, bundleUUID) => {
    try {
      await deleteSupportBundleRequest(universeUUID, bundleUUID, useV2Api);
    } catch (error) {
      handleServerError(error, { customErrorLabel: 'Failed to delete support bundle' });
      return;
    }
    await supportBundlesListQuery.refetch();
  };

  const handleDownloadBundle = (universeUUID, bundleUUID) => {
    downloadSupportBundleRequest(universeUUID, bundleUUID, useV2Api);
  };

  const onClose = () => {
    queryClient.removeQueries('estimatedSupportBundleSize');
    closeModal();
  };

  const isSubmitDisabled =
    currentStep === SupportBundleStep.CREATE_FORM &&
    (payload?.components?.length === 0 || createSupportBundleMutation.isLoading);

  if (supportBundlesListQuery.isLoading) {
    return (
      <YBModal
        className="universe-support-bundle"
        title="Support Bundle"
        open
        onClose={onClose}
        overrideHeight="fit-content"
        cancelLabel="Close"
        buttonProps={{ primary: { disabled: true } }}
      >
        <YBLoading />
      </YBModal>
    );
  }

  if (supportBundlesListQuery.isError && !supportBundlesListQuery.data) {
    return (
      <YBModal
        className="universe-support-bundle"
        title="Support Bundle"
        open
        onClose={onClose}
        overrideHeight="fit-content"
        cancelLabel="Close"
        buttonProps={{ primary: { disabled: true } }}
      >
        <YBErrorIndicator customErrorMessage="Failed to fetch support bundles." />
      </YBModal>
    );
  }

  return (
    <YBModal
      className="universe-support-bundle"
      title="Support Bundle"
      open
      onClose={onClose}
      overrideHeight="fit-content"
      cancelLabel="Close"
      submitLabel={currentStep === SupportBundleStep.CREATE_FORM ? 'Create Bundle' : undefined}
      onSubmit={
        currentStep === SupportBundleStep.CREATE_FORM
          ? () => {
              createSupportBundleMutation.mutate(payload);
            }
          : undefined
      }
      isSubmitting={createSupportBundleMutation.isLoading}
      buttonProps={{ primary: { disabled: isSubmitDisabled } }}
    >
      <div className="universe-support-bundle-body">
        {currentStep === SupportBundleStep.EMPTY && (
          <FirstStep
            onCreateSupportBundle={() => {
              setIsOnCreateForm(true);
            }}
            universeUUID={universeDetails.universeUUID}
            useV2Api={useV2Api}
          />
        )}
        {currentStep === SupportBundleStep.CREATE_FORM && (
          <SecondStep
            onOptionsChange={(selectedOptions) => {
              if (selectedOptions) {
                setPayload(selectedOptions);
              } else {
                setPayload(defaultOptions);
              }
            }}
            isPerfAdvisorRegistered={isPerfAdvisorRegistered}
            payload={payload}
            universeUUID={universeDetails.universeUUID}
            isK8sUniverse={isK8sUniverse}
            universeStatus={getUniverseStatus(currentUniverse)}
            useV2Api={useV2Api}
          />
        )}
        {currentStep === SupportBundleStep.LIST && (
          <ThirdStep
            handleDownloadBundle={(bundleUUID) =>
              handleDownloadBundle(universeDetails.universeUUID, bundleUUID)
            }
            handleDeleteBundle={(bundleUUID) =>
              handleDeleteBundle(universeDetails.universeUUID, bundleUUID)
            }
            supportBundles={supportBundles}
            onCreateSupportBundle={() => {
              setIsOnCreateForm(true);
            }}
            universeUUID={universeDetails.universeUUID}
            useV2Api={useV2Api}
            totalCount={totalCount}
            activePage={activePage}
            onPageChange={setActivePage}
          />
        )}
      </div>
    </YBModal>
  );
};

/**
 * Kept for SecondStep estimate query. Routes to v1 or v2 based on useV2Api.
 */
export function fetchEstimatedSupportBundleSize(universeUUID, supportBundle, useV2Api = false) {
  return estimateSupportBundleSizeRequest(universeUUID, supportBundle, useV2Api);
}

export default UniverseSupportBundleModal;
