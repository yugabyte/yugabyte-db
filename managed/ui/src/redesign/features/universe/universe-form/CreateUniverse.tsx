import { FC, useContext } from 'react';
import { useQueryClient } from 'react-query';
import { useSelector } from 'react-redux';
import { browserHistory } from 'react-router';
import { useTranslation } from 'react-i18next';
import { useUpdateEffect, useEffectOnce } from 'react-use';
import { UniverseFormContext } from './UniverseFormContainer';
import { UniverseForm } from './form/UniverseForm';
import { YBLoading } from '../../../../components/common/indicators';
import { getPlacements } from './form/fields/PlacementsField/PlacementsFieldHelper';
import {
  createUniverse,
  filterFormDataByClusterType,
  getPrimaryInheritedValues,
  getUserIntent
} from './utils/helpers';
import {
  Cluster,
  ClusterType,
  ClusterModes,
  NodeDetails,
  UniverseFormData,
  CloudType,
  UniverseConfigure,
  DEFAULT_FORM_DATA
} from './utils/dto';

export const CreateUniverse: FC = () => {
  const { t } = useTranslation();
  const [contextState, contextMethods]: any = useContext(UniverseFormContext);
  const {
    asyncFormData,
    clusterType,
    isLoading,
    primaryFormData,
    universeConfigureTemplate
  } = contextState;
  const {
    initializeForm,
    setPrimaryFormData,
    setAsyncFormData,
    setLoader,
    toggleClusterType
  } = contextMethods;
  const featureFlags = useSelector((state: any) => state.featureFlags);
  const isPrimary = clusterType === ClusterType.PRIMARY;

  const queryClient = useQueryClient();

  useEffectOnce(() => {
    initializeForm({
      clusterType: ClusterType.PRIMARY,
      mode: ClusterModes.CREATE,
      newUniverse: true
    });
  });

  //Toggle ClusterType after Primary data is set to avoid bad effects
  useUpdateEffect(() => {
    toggleClusterType(ClusterType.ASYNC);
  }, [primaryFormData]);

  //Toggle ClusterType after Async data is set to avoid bad effects
  useUpdateEffect(() => {
    toggleClusterType(ClusterType.PRIMARY);
  }, [asyncFormData]);

  //Set Loader to false after all the updates are done
  useUpdateEffect(() => {
    setLoader(false);
  }, [clusterType]);

  const handleClearRR = () => {
    const primaryClusterUUID = universeConfigureTemplate.clusters.find(
      (cluster: Cluster) => cluster.clusterType === ClusterType.PRIMARY
    ).uuid;
    //1. Remove async cluster from clusters array
    //2. Remove nodes related to async cluster
    initializeForm({
      clusterType: ClusterType.PRIMARY,
      asyncFormData: null,
      universeConfigureTemplate: {
        ...universeConfigureTemplate,
        clusters: universeConfigureTemplate.clusters.filter(
          (cluster: Cluster) => cluster.clusterType === ClusterType.PRIMARY
        ),
        nodeDetailsSet: universeConfigureTemplate.nodeDetailsSet.filter(
          (node: NodeDetails) => node.placementUuid === primaryClusterUUID
        )
      }
    });
  };

  const onCancel = () => browserHistory.push('/universes');

  const onSubmit = (primaryData: UniverseFormData, asyncData: UniverseFormData) => {
    const configurePayload: UniverseConfigure = {
      clusterOperation: ClusterModes.CREATE,
      currentClusterType: contextState.clusterType,
      rootCA: primaryData.instanceConfig.rootCA,
      userAZSelected: false,
      resetAZConfig: false,
      enableYbc: featureFlags.released.enableYbc || featureFlags.test.enableYbc,
      communicationPorts: primaryData.advancedConfig.communicationPorts,
      mastersInDefaultRegion: !!primaryData?.cloudConfig?.mastersInDefaultRegion,
      encryptionAtRestConfig: {
        key_op: primaryData.instanceConfig.enableEncryptionAtRest ? 'ENABLE' : 'UNDEFINED'
      },
      clusters: [
        {
          clusterType: ClusterType.PRIMARY,
          userIntent: getUserIntent({ formData: primaryData }, ClusterType.PRIMARY, featureFlags),
          placementInfo: {
            cloudList: [
              {
                uuid: primaryData.cloudConfig.provider?.uuid as string,
                code: primaryData.cloudConfig.provider?.code as CloudType,
                regionList: getPlacements(primaryData),
                defaultRegion: primaryData.cloudConfig.defaultRegion
              }
            ]
          }
        }
      ]
    };
    if (asyncData) {
      configurePayload.clusters?.push({
        clusterType: ClusterType.ASYNC,

        userIntent: getUserIntent(
          {
            formData: { ...asyncData, ...getPrimaryInheritedValues(primaryData) } //copy primary field values (inherited fields) to RR during fresh Universe+RR creation
          },
          ClusterType.ASYNC,
          featureFlags
        ),
        placementInfo: {
          cloudList: [
            {
              uuid: asyncData.cloudConfig.provider?.uuid as string,
              code: asyncData.cloudConfig.provider?.code as CloudType,
              regionList: getPlacements(asyncData)
            }
          ]
        }
      });
    }

    if (
      primaryData?.instanceConfig?.enableEncryptionAtRest &&
      primaryData?.instanceConfig?.kmsConfig &&
      configurePayload.encryptionAtRestConfig
    ) {
      configurePayload.encryptionAtRestConfig.configUUID = primaryData.instanceConfig.kmsConfig;
    }
    createUniverse({ configurePayload, universeContextData: contextState });
    setTimeout(()=>{
      queryClient.invalidateQueries('user_permissions');
    }, 2000);
  };

  if (isLoading) return <YBLoading />;

  if (isPrimary)
    return (
      <UniverseForm
        defaultFormData={primaryFormData ?? DEFAULT_FORM_DATA}
        onFormSubmit={(data: UniverseFormData) =>
          onSubmit(
            data,
            asyncFormData
              ? { ...asyncFormData, ...getPrimaryInheritedValues(primaryFormData) }
              : null
          )
        }
        submitLabel={t('common.create')}
        onCancel={onCancel}
        onDeleteRR={handleClearRR} //Clearing configured RR ( UI only operation)
        onClusterTypeChange={(data: UniverseFormData) => {
          setLoader(true);
          setPrimaryFormData(data);
        }}
        isNewUniverse //Mandatory flag for new universe flow
        key={ClusterType.PRIMARY}
      />
    );
  else
    return (
      <UniverseForm
        defaultFormData={
          asyncFormData
            ? { ...asyncFormData, ...getPrimaryInheritedValues(primaryFormData) } //Not all the fields needs to be copied from primary -> async
            : filterFormDataByClusterType(primaryFormData, ClusterType.ASYNC)
        }
        onFormSubmit={(data: UniverseFormData) => onSubmit(primaryFormData, data)}
        onCancel={onCancel}
        onDeleteRR={handleClearRR} //Clearing configured RR ( UI only operation )
        submitLabel={t('common.create')}
        onClusterTypeChange={(data: UniverseFormData) => {
          setLoader(true);
          setAsyncFormData(data);
        }}
        isNewUniverse //Mandatory flag for new universe flow
        key={ClusterType.ASYNC}
      />
    );
};
