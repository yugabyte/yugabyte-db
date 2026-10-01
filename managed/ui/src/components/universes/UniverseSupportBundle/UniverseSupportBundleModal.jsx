// Copyright (c) YugabyteDB, Inc.

import { useCallback, useEffect, useRef, useState } from 'react';
import { connect, useDispatch, useSelector } from 'react-redux';
import { useQuery, useQueryClient } from 'react-query';
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
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import { getSupportBundles } from '../../../selector/supportBundle';
import { setListSupportBundle } from '../../../actions/supportBundle';
import { createErrorMessage } from '../../../utils/ObjectUtils';
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

const stepsObj = {
  firstStep: 'firstStep',
  secondStep: 'secondStep',
  thirdStep: 'thirdStep'
};

const POLLING_INTERVAL = 10000; // ten seconds

const isPerfAdvisorNotFound = (error) =>
  error?.request?.status === 404 || error?.response?.status === 404;

export const UniverseSupportBundleModal = (props) => {
  const { currentUniverse, closeModal } = props;
  const dispatch = useDispatch();

  useEffect(() => {
    return () => {
      dispatch(setListSupportBundle({ data: [], status: 200 }));
    };
  }, [dispatch]);

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

  const useV2Api = isSupportBundleUiV2Enabled(
    globalRuntimeConfigsQuery.data?.configEntries
  );

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
  const [steps, setSteps] = useState(stepsObj.firstStep);
  const [activePage, setActivePage] = useState(1);
  const [totalCount, setTotalCount] = useState(0);
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
  const dispatch = useDispatch();
  const [supportBundles] = useSelector(getSupportBundles);
  const queryClient = useQueryClient();
  const [hasListed, setHasListed] = useState(false);
  const [listError, setListError] = useState(false);
  const hasListedSuccessfullyRef = useRef(false);

  const refreshSupportBundles = useCallback(
    (universeUUID, page = activePage) => {
      const offset = useV2Api ? (page - 1) * SUPPORT_BUNDLE_PAGE_SIZE : 0;
      return listSupportBundles(universeUUID, useV2Api, offset)
        .then(({ bundles, totalCount: nextTotalCount }) => {
          setTotalCount(nextTotalCount);
          if (useV2Api) {
            const lastPage = Math.max(
              1,
              Math.ceil(nextTotalCount / SUPPORT_BUNDLE_PAGE_SIZE)
            );
            if (page > lastPage) {
              setActivePage(lastPage);
              setHasListed(true);
              hasListedSuccessfullyRef.current = true;
              setListError(false);
              return;
            }
          }
          dispatch(setListSupportBundle({ data: bundles, status: 200 }));
          setHasListed(true);
          hasListedSuccessfullyRef.current = true;
          setListError(false);
        })
        .catch((error) => {
          setHasListed(true);
          if (!hasListedSuccessfullyRef.current) {
            setListError(true);
            return;
          }
          toast.error(createErrorMessage(error));
        });
    },
    [dispatch, useV2Api, activePage]
  );

  useEffect(() => {
    refreshSupportBundles(universeDetails.universeUUID, activePage);
  }, [refreshSupportBundles, universeDetails.universeUUID, activePage]);

  useEffect(() => {
    if (!hasListed) {
      return undefined;
    }
    if (totalCount === 0) {
      if (steps !== stepsObj.secondStep) {
        setSteps(stepsObj.firstStep);
      }
      return undefined;
    }
    if (steps !== stepsObj.secondStep) {
      setSteps(stepsObj.thirdStep);
    }
    if (
      supportBundles &&
      Array.isArray(supportBundles) &&
      supportBundles.find((supportBundle) => supportBundle.status === 'Running') !== undefined
    ) {
      const timeoutId = setTimeout(() => {
        refreshSupportBundles(universeDetails.universeUUID, activePage);
      }, POLLING_INTERVAL);
      return () => {
        clearTimeout(timeoutId);
      };
    }
    return undefined;
  }, [
    hasListed,
    totalCount,
    supportBundles,
    refreshSupportBundles,
    universeDetails.universeUUID,
    activePage
  ]);

  const saveSupportBundle = async (universeUUID) => {
    try {
      await createSupportBundleRequest(universeUUID, payload, useV2Api);
    } catch (error) {
      if (error?.response?.status === 403) {
        toast.error(RBAC_ERR_MSG_NO_PERM, { autoClose: 3000 });
      } else {
        toast.error(createErrorMessage(error));
      }
    }
    handleStepChange(stepsObj.thirdStep);
    setActivePage(1);
    refreshSupportBundles(universeUUID, 1);
    setPayload(defaultOptions);
  };

  const handleStepChange = (step) => {
    setSteps(step);
  };

  const handleDeleteBundle = async (universeUUID, bundleUUID) => {
    try {
      await deleteSupportBundleRequest(universeUUID, bundleUUID, useV2Api);
    } catch (error) {
      toast.error(createErrorMessage(error));
    }
    refreshSupportBundles(universeUUID, activePage);
  };

  const handleDownloadBundle = (universeUUID, bundleUUID) => {
    downloadSupportBundleRequest(universeUUID, bundleUUID, useV2Api);
  };

  const onClose = () => {
    queryClient.removeQueries('estimatedSupportBundleSize');
    closeModal();
  };

  const isSubmitDisabled = steps === stepsObj.secondStep && payload?.components?.length === 0;

  if (!hasListed) {
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

  if (listError) {
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
      submitLabel={steps === stepsObj.secondStep ? 'Create Bundle' : undefined}
      onSubmit={
        steps === stepsObj.secondStep
          ? () => {
              saveSupportBundle(universeDetails.universeUUID);
            }
          : undefined
      }
      buttonProps={{ primary: { disabled: isSubmitDisabled } }}
    >
      <div className="universe-support-bundle-body">
        {steps === stepsObj.firstStep && (
          <FirstStep
            onCreateSupportBundle={() => {
              handleStepChange(stepsObj.secondStep);
            }}
            universeUUID={universeDetails.universeUUID}
            useV2Api={useV2Api}
          />
        )}
        {steps === stepsObj.secondStep && (
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
        {steps === stepsObj.thirdStep && (
          <ThirdStep
            handleDownloadBundle={(bundleUUID) =>
              handleDownloadBundle(universeDetails.universeUUID, bundleUUID)
            }
            handleDeleteBundle={(bundleUUID) =>
              handleDeleteBundle(universeDetails.universeUUID, bundleUUID)
            }
            supportBundles={supportBundles}
            onCreateSupportBundle={() => {
              handleStepChange(stepsObj.secondStep);
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

function mapStateToProps(state) {
  return {
    supportBundle: state.supportBundle
  };
}

export default connect(mapStateToProps)(UniverseSupportBundleModal);
