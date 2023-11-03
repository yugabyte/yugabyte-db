// Copyright (c) YugaByte, Inc.

import { Fragment, useCallback, useEffect, useState } from 'react';
import axios from 'axios';
import { connect, useDispatch, useSelector } from 'react-redux';
import { FirstStep } from './FirstStep/FirstStep';
import { SecondStep, updateOptions } from './SecondStep/SecondStep';
import { ThirdStep } from './ThirdStep/ThirdStep';
import { YBModal, YBButton } from '../../common/forms/fields';
import { ROOT_URL } from '../../../config';
import { getSupportBundles } from '../../../selector/supportBundle';
import { isEmptyObject } from '../../../utils/ObjectUtils';
import {
  createSupportBundle,
  listSupportBundle,
  setListSupportBundle
} from '../../../actions/supportBundle';

import 'react-bootstrap-table/css/react-bootstrap-table.css';
import './UniverseSupportBundle.scss';
import { filterTypes } from '../../metrics/MetricsComparisonModal/ComparisonFilterContextProvider';
import { isKubernetesUniverse } from '../../../utils/UniverseUtils';
import { getUniverseStatus } from '../helpers/universeHelpers';

const stepsObj = {
  firstStep: 'firstStep',
  secondStep: 'secondStep',
  thirdStep: 'thirdStep'
};

const POLLING_INTERVAL = 3000; // ten seconds

export const UniverseSupportBundle = (props) => {
  const {
    currentUniverse: { universeDetails },
    button,
    closeModal,
    modal: { showModal, visibleModal }
  } = props;
  const [steps, setSteps] = useState(stepsObj.firstStep);
  const defaultOptions = updateOptions(
    filterTypes[0].value,
    [true, true, true, true, true, true, true, true, true, true],
    () => {}
  );
  const [payload, setPayload] = useState(defaultOptions);

  const isK8sUniverse = isKubernetesUniverse(props.currentUniverse);

  const dispatch = useDispatch();
  const [supportBundles] = useSelector(getSupportBundles);

  const resetSteps = () => {
    if (supportBundles && Array.isArray(supportBundles) && supportBundles.length === 0) {
      setSteps(stepsObj.firstStep);
    } else {
      setSteps(stepsObj.thirdStep);
    }
  };

  const listSupportBundle = useCallback(
    (universeUUID) => {
      dispatch(getSupportBundle(universeUUID)).then((response) => {
        dispatch(setListSupportBundle(response.payload));
      });
    },
    [dispatch]
  );

  useEffect(() => {
    listSupportBundle(universeDetails.universeUUID);
  }, [listSupportBundle, universeDetails.universeUUID]);

  useEffect(() => {
    if (supportBundles && Array.isArray(supportBundles) && supportBundles.length === 0) {
      setSteps(stepsObj.firstStep);
    } else {
      setSteps(stepsObj.thirdStep);
      if (
        supportBundles &&
        Array.isArray(supportBundles) &&
        supportBundles.find((supportBundle) => supportBundle.status === 'Running') !== undefined
      ) {
        setTimeout(() => {
          listSupportBundle(universeDetails.universeUUID);
        }, POLLING_INTERVAL);
      }
    }
  }, [supportBundles, listSupportBundle, universeDetails.universeUUID]);

  const saveSupportBundle = (universeUUID) => {
    dispatch(crateSupportBundle(universeUUID, payload)).then(() => {
      handleStepChange(stepsObj.thirdStep);
      listSupportBundle(universeUUID);
      setPayload(defaultOptions);
    });
  };

  const handleStepChange = (step) => {
    setSteps(step);
  };

  const handleDeleteBundle = (universeUUID, bundleUUID) => {
    dispatch(deleteBundleByBundleUUID(universeUUID, bundleUUID)).then(() => {
      listSupportBundle(universeUUID);
    });
  };

  const handleDownloadBundle = (universeUUID, bundleUUID) => {
    downloadSupportBundle(universeUUID, bundleUUID);
  };

  const isSubmitDisabled = () => {
    if (steps === stepsObj.secondStep) {
      return payload?.components?.length === 0;
    }
    return false;
  };

  return (
    <Fragment>
      {isEmptyObject(button) ? (
        <YBButton btnText={'Support Bundle'} btnClass={'btn btn-default open-modal-btn'} />
      ) : (
        button
      )}
      <YBModal
        className="universe-support-bundle"
        title={'Support Bundle'}
        visible={showModal && visibleModal === 'supportBundleModal'}
        onHide={() => {
          resetSteps();
          closeModal();
        }}
        cancelLabel="Close"
        showCancelButton
        submitLabel={steps === stepsObj.secondStep ? 'Create Bundle' : undefined}
        onFormSubmit={
          steps === stepsObj.secondStep
            ? () => {
                saveSupportBundle(universeDetails.universeUUID);
              }
            : undefined
        }
        disableSubmit={isSubmitDisabled()}
      >
        {steps === stepsObj.firstStep && (
          <FirstStep
            onCreateSupportBundle={() => {
              handleStepChange(stepsObj.secondStep);
            }}
            universeUUID={universeDetails.universeUUID}
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
            isK8sUniverse={isK8sUniverse}
            universeStatus={getUniverseStatus(props.currentUniverse)}
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
          />
        )}
      </YBModal>
    </Fragment>
  );
};

export function getSupportBundle(universeUUID) {
  const customerUUID = localStorage.getItem('customerId');
  const endpoint = `${ROOT_URL}/customers/${customerUUID}/universes/${universeUUID}/support_bundle`;
  const request = axios.get(endpoint);
  return listSupportBundle(request);
}

export function crateSupportBundle(universeUUID, supportBundle) {
  const customerUUID = localStorage.getItem('customerId');
  const endpoint = `${ROOT_URL}/customers/${customerUUID}/universes/${universeUUID}/support_bundle`;
  const request = axios.post(endpoint, supportBundle);
  return createSupportBundle(request);
}

export function deleteBundleByBundleUUID(universeUUID, supportBundleUUID) {
  const customerUUID = localStorage.getItem('customerId');
  const endpoint = `${ROOT_URL}/customers/${customerUUID}/universes/${universeUUID}/support_bundle/${supportBundleUUID}`;
  const request = axios.delete(endpoint);
  return createSupportBundle(request);
}

export function downloadSupportBundle(universeUUID, supportBundleUUID) {
  const customerUUID = localStorage.getItem('customerId');
  const endpoint = `${ROOT_URL}/customers/${customerUUID}/universes/${universeUUID}/support_bundle/${supportBundleUUID}/download`;
  window.open(endpoint, '_blank');
}

function mapStateToProps(state) {
  return {
    supportBundle: state.supportBundle
  };
}

export default connect(mapStateToProps)(UniverseSupportBundle);
