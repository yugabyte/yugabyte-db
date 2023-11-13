import { FC, useContext, useState } from 'react';
import _ from 'lodash';
import { useQuery } from 'react-query';
import { browserHistory } from 'react-router';
import { toast } from 'react-toastify';
import { UniverseFormContext } from './UniverseFormContainer';
import { UniverseForm } from './form/UniverseForm';
import { FullMoveModal, PlacementModal, ResizeNodeModal, SmartResizeModal } from './action-modals';
import { YBLoading } from '../../../../components/common/indicators';
import { api, QUERY_KEY } from './utils/api';
import { getPlacements } from './form/fields/PlacementsField/PlacementsFieldHelper';
import {
  createErrorMessage,
  getPrimaryFormData,
  transformTagsArrayToObject,
  transitToUniverse
} from './utils/helpers';
import {
  Cluster,
  ClusterModes,
  ClusterType,
  CloudType,
  MasterPlacementMode,
  UniverseConfigure,
  UniverseFormData,
  UniverseDetails
} from './utils/dto';
import {
  DEVICE_INFO_FIELD,
  MASTER_DEVICE_INFO_FIELD,
  INSTANCE_TYPE_FIELD,
  TSERVER_K8_NODE_SPEC_FIELD,
  MASTER_K8_NODE_SPEC_FIELD,
  MASTER_INSTANCE_TYPE_FIELD,
  MASTER_PLACEMENT_FIELD,
  PROVIDER_FIELD,
  REGIONS_FIELD,
  REPLICATION_FACTOR_FIELD,
  TOAST_AUTO_DISMISS_INTERVAL,
  TOTAL_NODES_FIELD,
  USER_TAGS_FIELD,
  SPOT_INSTANCE_FIELD
} from './utils/constants';

export enum UPDATE_ACTIONS {
  FULL_MOVE = 'FULL_MOVE',
  SMART_RESIZE = 'SMART_RESIZE',
  SMART_RESIZE_NON_RESTART = 'SMART_RESIZE_NON_RESTART',
  UPDATE = 'UPDATE'
}
interface EditUniverseProps {
  uuid: string;
  isViewMode: boolean;
}

export const EditUniverse: FC<EditUniverseProps> = ({ uuid, isViewMode }) => {
  const [contextState, contextMethods]: any = useContext(UniverseFormContext);
  const { isLoading, universeConfigureTemplate } = contextState;
  const { initializeForm, setUniverseResourceTemplate } = contextMethods;

  //Local states
  const [showFMModal, setFMModal] = useState(false); //FM -> Full Move
  const [showRNModal, setRNModal] = useState(false); //RN -> Resize Nodes
  const [showSRModal, setSRModal] = useState(false); //SR -> Smart Resize
  const [showPlacementModal, setPlacementModal] = useState(false);
  const [universePayload, setUniversePayload] = useState<UniverseDetails | null>(null);

  const { isLoading: isUniverseLoading, data: originalData } = useQuery(
    [QUERY_KEY.fetchUniverse, uuid],
    () => api.fetchUniverse(uuid),
    {
      onSuccess: async (resp) => {
        try {
          const configureResponse = await api.universeConfigure({
            ..._.cloneDeep(resp.universeDetails),
            clusterOperation: ClusterModes.EDIT,
            currentClusterType: ClusterType.PRIMARY
          });
          initializeForm({
            clusterType: ClusterType.PRIMARY,
            mode: ClusterModes.EDIT,
            isViewMode,
            universeConfigureTemplate: _.cloneDeep(configureResponse)
          });
          //set Universe Resource Template
          const resourceResponse = await api.universeResource({
            ..._.cloneDeep(resp.universeDetails),
            currentClusterType: ClusterType.PRIMARY
          });
          setUniverseResourceTemplate(resourceResponse);
        } catch (error) {
          toast.error(createErrorMessage(error), { autoClose: TOAST_AUTO_DISMISS_INTERVAL });
        }
      },
      onError: (error) => {
        console.error(error);
        transitToUniverse();
      }
    }
  );

  const onCancel = () => browserHistory.push(`/universes/${uuid}`);

  const submitEditUniverse = async (finalPayload: UniverseConfigure) => {
    try {
      let response = await api.editUniverse(finalPayload, uuid);
      response && transitToUniverse(uuid);
    } catch (error) {
      toast.error(createErrorMessage(error), { autoClose: TOAST_AUTO_DISMISS_INTERVAL });
      console.error(error);
    }
  };

  if (isUniverseLoading || isLoading || !originalData?.universeDetails) return <YBLoading />;

  const initialFormData = getPrimaryFormData(originalData.universeDetails);

  const onSubmit = async (formData: UniverseFormData) => {
    if (!_.isEqual(formData, initialFormData)) {
      const payload = _.cloneDeep(universeConfigureTemplate);
      payload.currentClusterType = ClusterType.PRIMARY;
      payload.clusterOperation = ClusterModes.EDIT;
      const primaryIndex = payload.clusters.findIndex(
        (c: Cluster) => c.clusterType === ClusterType.PRIMARY
      );
      const asyncIndex = payload.clusters.findIndex(
        (c: Cluster) => c.clusterType === ClusterType.ASYNC
      );
      const masterPlacement = _.get(formData, MASTER_PLACEMENT_FIELD);
      //update fields which are allowed to edit
      const userIntent = payload.clusters[primaryIndex].userIntent;
      userIntent.regionList = _.get(formData, REGIONS_FIELD);
      userIntent.numNodes = _.get(formData, TOTAL_NODES_FIELD);
      userIntent.replicationFactor = _.get(formData, REPLICATION_FACTOR_FIELD);
      userIntent.instanceType = _.get(formData, INSTANCE_TYPE_FIELD);
      userIntent.useSpotInstance = _.get(formData, SPOT_INSTANCE_FIELD);
      userIntent.deviceInfo = _.get(formData, DEVICE_INFO_FIELD);
      userIntent.instanceTags = transformTagsArrayToObject(_.get(formData, USER_TAGS_FIELD, []));
      userIntent.dedicatedNodes = masterPlacement === MasterPlacementMode.DEDICATED;

      const isK8sUniverse = _.get(formData, PROVIDER_FIELD).code === CloudType.kubernetes;
      //if async cluster exists
      if (asyncIndex > -1) {
        //copy user tags value from primary to read replica
        payload.clusters[asyncIndex].userIntent.instanceTags = transformTagsArrayToObject(
          _.get(formData, USER_TAGS_FIELD, [])
        );
      }

      // Update master instance type and device information in case of dedicated mode
      if (userIntent.dedicatedNodes) {
        userIntent.masterInstanceType = _.get(formData, MASTER_INSTANCE_TYPE_FIELD);
        userIntent.masterDeviceInfo = _.get(formData, MASTER_DEVICE_INFO_FIELD);
      }

      if (isK8sUniverse && masterPlacement === MasterPlacementMode.DEDICATED) {
        userIntent.masterK8SNodeResourceSpec = userIntent.dedicatedNodes
          ? _.get(formData, MASTER_K8_NODE_SPEC_FIELD)
          : null;
        userIntent.tserverK8SNodeResourceSpec = _.get(formData, TSERVER_K8_NODE_SPEC_FIELD);
      }

      payload.clusters[primaryIndex].placementInfo.cloudList[0].regionList = getPlacements(
        formData
      );
      const finalPayload = await api.universeConfigure(payload);
      const { updateOptions } = finalPayload;
      setUniversePayload(finalPayload);

      if (!isK8sUniverse) {
        if (
          _.intersection(updateOptions, [UPDATE_ACTIONS.SMART_RESIZE, UPDATE_ACTIONS.FULL_MOVE])
            .length > 1
        )
          setSRModal(true);
        else if (updateOptions.includes(UPDATE_ACTIONS.SMART_RESIZE_NON_RESTART)) setRNModal(true);
        else if (updateOptions.includes(UPDATE_ACTIONS.FULL_MOVE)) setFMModal(true);
        else setPlacementModal(true);
      } else submitEditUniverse(finalPayload);
    } else
      toast.warn('Nothing to update - no fields changed', {
        autoClose: TOAST_AUTO_DISMISS_INTERVAL
      });
  };

  return (
    <>
      <UniverseForm
        defaultFormData={initialFormData}
        onFormSubmit={onSubmit}
        onCancel={onCancel}
        universeUUID={uuid}
        isViewMode={isViewMode}
      />
      {universePayload && (
        <>
          {showRNModal && (
            <ResizeNodeModal
              open={showRNModal}
              universeData={universePayload}
              onClose={() => setRNModal(false)}
            />
          )}
          {showSRModal && (
            <SmartResizeModal
              open={showSRModal}
              oldConfigData={originalData.universeDetails}
              newConfigData={universePayload}
              onClose={() => setSRModal(false)}
              handleSmartResize={() => {
                setSRModal(false);
                setRNModal(true);
              }}
              handleFullMove={() => submitEditUniverse(universePayload)}
            />
          )}
          {showFMModal && (
            <FullMoveModal
              open={showFMModal}
              oldConfigData={originalData.universeDetails}
              newConfigData={universePayload}
              onClose={() => setFMModal(false)}
              onSubmit={() => submitEditUniverse(universePayload)}
            />
          )}
          {showPlacementModal && (
            <PlacementModal
              open={showPlacementModal}
              oldConfigData={originalData.universeDetails}
              newConfigData={universePayload}
              onClose={() => setPlacementModal(false)}
              onSubmit={() => submitEditUniverse(universePayload)}
            />
          )}
        </>
      )}
    </>
  );
};
