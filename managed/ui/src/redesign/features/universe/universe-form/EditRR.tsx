import { FC, useContext, useState } from 'react';
import _ from 'lodash';
import { useQuery } from 'react-query';
import { useSelector } from 'react-redux';
import { browserHistory } from 'react-router';
import { toast } from 'react-toastify';
import { UniverseFormContext } from './UniverseFormContainer';
import { UniverseForm } from './form/UniverseForm';
import {
  DeleteClusterModal,
  FullMoveModal,
  KubernetesPlacementModal,
  PlacementModal,
  ResizeNodeModal,
  SmartResizeModal
} from './action-modals';
import { YBLoading } from '../../../../components/common/indicators';
import { api, QUERY_KEY } from './utils/api';
import { getPlacements } from './form/fields/PlacementsField/PlacementsFieldHelper';
import {
  editReadReplica,
  getAsyncCluster,
  getAsyncFormData,
  getUserIntent,
  createErrorMessage,
  transitToUniverse
} from './utils/helpers';
import {
  CloudType,
  ClusterModes,
  ClusterType,
  UpdateActions,
  UniverseDetails,
  UniverseFormData
} from './utils/dto';
import { PROVIDER_FIELD, TOAST_AUTO_DISMISS_INTERVAL } from './utils/constants';
import { providerQueryKey, api as helperApi } from '../../../helpers/api';

interface EditReadReplicaProps {
  uuid: string;
  isViewMode: boolean;
}

export const EditReadReplica: FC<EditReadReplicaProps> = ({ uuid, isViewMode }) => {
  const featureFlags = useSelector((state: any) => state.featureFlags);
  const [contextState, contextMethods]: any = useContext(UniverseFormContext);
  const { initializeForm, setUniverseResourceTemplate } = contextMethods;

  //Local states
  const [showFMModal, setFMModal] = useState(false); //FM -> Full Move
  const [showSRModal, setSRModal] = useState(false); //SR -> Smart Resize
  const [showRNModal, setRNModal] = useState(false); //RN -> Resize Nodes
  const [showPlacementModal, setPlacementModal] = useState(false);
  const [showK8Modal, setK8Modal] = useState(false);
  const [showDeleteRRModal, setShowDeleteRRModal] = useState(false);
  const [universePayload, setUniversePayload] = useState<UniverseDetails | null>(null);

  const { isLoading, data: universe } = useQuery(
    [QUERY_KEY.fetchUniverse, uuid],
    () => api.fetchUniverse(uuid),
    {
      onSuccess: async (resp) => {
        initializeForm({
          clusterType: ClusterType.ASYNC,
          mode: ClusterModes.EDIT,
          isViewMode,
          universeConfigureTemplate: _.cloneDeep(resp.universeDetails)
        });
        try {
          //set Universe Resource Template
          const resourceResponse = await api.universeResource(_.cloneDeep(resp.universeDetails));
          setUniverseResourceTemplate(resourceResponse);
        } catch (error) {
          toast.error(createErrorMessage(error), { autoClose: TOAST_AUTO_DISMISS_INTERVAL });
        }
      },
      onError: (error) => {
        console.error(error);
        transitToUniverse(); //redirect to /universes if universe with uuid doesnot exists
      }
    }
  );

  const asyncProviderUuid = universe?.universeDetails
    ? getAsyncCluster(universe?.universeDetails)?.userIntent?.provider ?? ''
    : '';
  const providerConfigQuery = useQuery(
    providerQueryKey.detail(asyncProviderUuid),
    () => helperApi.fetchProvider(asyncProviderUuid),
    { enabled: !!asyncProviderUuid }
  );

  const onCancel = () => browserHistory.push(`/universes/${uuid}`);

  const onSubmit = async (formData: UniverseFormData) => {
    const asyncCluster = getAsyncCluster(contextState.universeConfigureTemplate);
    const asyncUserIntent = {
      ...asyncCluster?.userIntent,
      ...getUserIntent({ formData }, ClusterType.ASYNC, featureFlags)
    };
    const configurePayload = {
      ...contextState.universeConfigureTemplate,
      clusterOperation: ClusterModes.EDIT,
      currentClusterType: ClusterType.ASYNC,
      expectedUniverseVersion: universe?.version,
      clusters: [
        {
          ...getAsyncCluster(contextState.universeConfigureTemplate),
          userIntent: asyncUserIntent,
          placementInfo: {
            cloudList: [
              {
                uuid: formData.cloudConfig.provider?.uuid as string,
                code: formData.cloudConfig.provider?.code as CloudType,
                regionList: getPlacements(formData)
              }
            ]
          }
        }
      ]
    };

    const finalPayload = await api.universeConfigure(configurePayload);
    setUniversePayload(finalPayload);
    const isK8sUniverse = _.get(formData, PROVIDER_FIELD).code === CloudType.kubernetes;
    const { updateOptions } = finalPayload;

    if (!isK8sUniverse) {
      if (
        _.intersection(updateOptions, [UpdateActions.SMART_RESIZE, UpdateActions.FULL_MOVE])
          .length > 1
      )
        setSRModal(true);
      else if (updateOptions.includes(UpdateActions.SMART_RESIZE_NON_RESTART)) setRNModal(true);
      else if (updateOptions.includes(UpdateActions.FULL_MOVE)) setFMModal(true);
      else setPlacementModal(true);
    } else setK8Modal(true);
  };

  if (isLoading || contextState.isLoading || providerConfigQuery.isLoading) {
    return <YBLoading />;
  }

  if (!universe?.universeDetails) return null;

  //get async form data and intitalize the form
  const initialFormData = getAsyncFormData(universe.universeDetails, providerConfigQuery.data);

  return (
    <>
      {showDeleteRRModal && (
        <DeleteClusterModal
          open={showDeleteRRModal}
          universeData={universe.universeDetails}
          onClose={() => setShowDeleteRRModal(false)}
        />
      )}
      <UniverseForm
        defaultFormData={initialFormData}
        onFormSubmit={(data: UniverseFormData) => onSubmit(data)}
        onCancel={onCancel}
        onDeleteRR={() => setShowDeleteRRModal(true)} //Deleting existing RR (API)
        universeUUID={uuid}
        isViewMode={isViewMode}
      />
      {universePayload && (
        <>
          {showRNModal && (
            <ResizeNodeModal
              open={showRNModal}
              isPrimary={false}
              universeData={universePayload}
              onClose={() => setRNModal(false)}
            />
          )}
          {showSRModal && (
            <SmartResizeModal
              open={showSRModal}
              isPrimary={false}
              oldConfigData={universe.universeDetails}
              newConfigData={universePayload}
              onClose={() => setSRModal(false)}
              handleSmartResize={() => {
                setSRModal(false);
                setRNModal(true);
              }}
              handleFullMove={() => editReadReplica(universePayload)}
            />
          )}
          {showFMModal && (
            <FullMoveModal
              open={showFMModal}
              isPrimary={false}
              oldConfigData={universe.universeDetails}
              newConfigData={universePayload}
              onClose={() => setFMModal(false)}
              onSubmit={() => editReadReplica(universePayload)}
            />
          )}
          {showPlacementModal && (
            <PlacementModal
              open={showPlacementModal}
              isPrimary={false}
              oldConfigData={universe.universeDetails}
              newConfigData={universePayload}
              onClose={() => setPlacementModal(false)}
              onSubmit={() => editReadReplica(universePayload)}
            />
          )}
          {showK8Modal && (
            <KubernetesPlacementModal
              open={showK8Modal}
              isPrimary={false}
              oldConfigData={universe.universeDetails}
              newConfigData={universePayload}
              onClose={() => setK8Modal(false)}
              onSubmit={() => editReadReplica(universePayload)}
            />
          )}
        </>
      )}
    </>
  );
};
