import { FC, useContext } from 'react';
import _ from 'lodash';
import { useQuery } from 'react-query';
import { useSelector } from 'react-redux';
import { browserHistory } from 'react-router';
import { useTranslation } from 'react-i18next';
import { UniverseFormContext } from './UniverseFormContainer';
import { UniverseForm } from './form/UniverseForm';
import { YBLoading } from '../../../../components/common/indicators';
import { api, QUERY_KEY } from './utils/api';
import { getPlacements } from './form/fields/PlacementsField/PlacementsFieldHelper';
import {
  createReadReplica,
  filterFormDataByClusterType,
  getAsyncCluster,
  getPrimaryCluster,
  getPrimaryFormData,
  getUserIntent,
  transitToUniverse
} from './utils/helpers';
import {
  CloudType,
  ClusterModes,
  ClusterType,
  NodeDetails,
  NodeState,
  UniverseFormData
} from './utils/dto';

interface CreateReadReplicaProps {
  uuid: string;
}

export const CreateReadReplica: FC<CreateReadReplicaProps> = ({ uuid }) => {
  const { t } = useTranslation();
  const featureFlags = useSelector((state: any) => state.featureFlags);
  const [contextState, contextMethods]: any = useContext(UniverseFormContext);
  const { initializeForm } = contextMethods;

  const { isLoading, data: universe } = useQuery(
    [QUERY_KEY.fetchUniverse, uuid],
    () => api.fetchUniverse(uuid),
    {
      onSuccess: async (resp) => {
        //initialize form
        initializeForm({
          clusterType: ClusterType.ASYNC,
          mode: ClusterModes.CREATE,
          universeConfigureTemplate: _.cloneDeep(resp.universeDetails)
        });
      },
      onError: (error) => {
        console.error(error);
        transitToUniverse(); //redirect to /universes if universe with uuid doesnot exists
      }
    }
  );

  const onCancel = () => browserHistory.push(`/universes/${uuid}`);

  const onSubmit = async (formData: UniverseFormData) => {
    const PRIMARY_CLUSTER = getPrimaryCluster(contextState.universeConfigureTemplate);
    const ASYNC_CLUSTER = {
      userIntent: getUserIntent({ formData }, ClusterType.ASYNC, featureFlags),
      clusterType: ClusterType.ASYNC,
      placementInfo: {
        cloudList: [
          {
            uuid: formData.cloudConfig.provider?.uuid as string,
            code: formData.cloudConfig.provider?.code as CloudType,
            regionList: getPlacements(formData)
          }
        ]
      }
    };

    //make a final configure call to check if everything is okay
    const configurePayload = {
      ...contextState.universeConfigureTemplate,
      clusterOperation: ClusterModes.CREATE,
      currentClusterType: ClusterType.ASYNC,
      clusters: [{ ...PRIMARY_CLUSTER }]
    };
    configurePayload?.clusters.push({
      ...getAsyncCluster(contextState.universeConfigureTemplate),
      ...ASYNC_CLUSTER
    });
    const configureData = await api.universeConfigure(configurePayload);

    //patch the final payload with response from configure call
    //remove all unwanted nodes in nodeDetailsSet
    const finalPayload = {
      ...configureData,
      expectedUniverseVersion: universe?.version,
      nodeDetailsSet: configureData.nodeDetailsSet
        .filter((node: NodeDetails) => node.state === NodeState.ToBeAdded)
        .map((node: NodeDetails) => ({
          ...node,
          cloudInfo: {
            ...node.cloudInfo,
            assignPublicIP: !!PRIMARY_CLUSTER?.userIntent.assignPublicIP
          }
        })),
      clusters: [
        {
          ...getAsyncCluster(configureData),
          ...ASYNC_CLUSTER
        }
      ]
    };
    createReadReplica(finalPayload);
  };

  if (isLoading || contextState.isLoading) return <YBLoading />;

  if (!universe) return null;

  //get primary form data, filter only async form fields and intitalize the form
  const primaryFormData = getPrimaryFormData(universe.universeDetails);
  const initialFormData = filterFormDataByClusterType(primaryFormData, ClusterType.ASYNC);

  return (
    <UniverseForm
      defaultFormData={initialFormData}
      submitLabel={t('universeForm.actions.addRR')}
      onFormSubmit={(data: UniverseFormData) => onSubmit(data)}
      onCancel={onCancel}
      universeUUID={uuid}
    />
  );
};
