import _ from 'lodash';
import { useContext, useState } from 'react';
import { browserHistory } from 'react-router';
import { ClusterOperation, WizardContext, WizardStepsFormData } from '../../UniverseWizard';
import {
  CloudType,
  ClusterType,
  FlagsArray,
  FlagsObject,
  PlacementAZ,
  PlacementRegion,
  UniverseConfigure,
  UniverseDetails
} from '../../../../helpers/dtos';
import { PlacementUI } from '../../fields/PlacementsField/PlacementsField';
import { api, QUERY_KEY } from '../../../../helpers/api';
import { useWhenMounted } from '../../../../helpers/hooks';

const tagsToPayload = (tags?: FlagsObject): FlagsArray => {
  const result = [];
  if (tags) {
    for (const [name, value] of Object.entries(tags)) {
      result.push({ name, value });
    }
  }
  return result;
};

const getPlacementRegion = (formData: WizardStepsFormData): PlacementRegion[] => {
  // remove gaps from placements list
  const placements: NonNullable<PlacementUI>[] = _.cloneDeep(
    _.compact(formData.cloudConfig.placements)
  );

  // update leaders flag "isAffinitized" in placements basing on form value
  const leaders = new Set(formData.dbConfig.preferredLeaders.flatMap((item) => item.uuid));
  placements.forEach((item) => (item.isAffinitized = leaders.has(item.uuid)));

  // prepare placements basing on user selection
  const regionMap: Record<string, PlacementRegion> = {};
  placements.forEach((item) => {
    const zone: PlacementAZ = _.omit(item, [
      'parentRegionId',
      'parentRegionCode',
      'parentRegionName'
    ]);
    if (Array.isArray(regionMap[item.parentRegionId]?.azList)) {
      regionMap[item.parentRegionId].azList.push(zone);
    } else {
      regionMap[item.parentRegionId] = {
        uuid: item.parentRegionId,
        code: item.parentRegionCode,
        name: item.parentRegionName,
        azList: [zone]
      };
    }
  });

  return Object.values(regionMap);
};

// fix some skewed/missing fields in config response
const patchConfigResponse = (
  data: UniverseDetails,
  operation: 'CREATE' | 'EDIT',
  clusterType: ClusterType,
  clusterIndex: number
) => {
  data.clusterOperation = operation;
  data.currentClusterType = clusterType;

  const userIntent = data.clusters[clusterIndex].userIntent;
  userIntent.instanceTags = tagsToPayload(userIntent.instanceTags as FlagsObject);
  userIntent.masterGFlags = tagsToPayload(userIntent.masterGFlags as FlagsObject);
  userIntent.tserverGFlags = tagsToPayload(userIntent.tserverGFlags as FlagsObject);
};

export const useLaunchUniverse = () => {
  const { formData, originalFormData, operation, universe } = useContext(WizardContext);
  const [isLoading, setIsLoading] = useState(false);
  const whenMounted = useWhenMounted();

  const launchUniverse = async () => {
    try {
      setIsLoading(true);

      switch (operation) {
        case ClusterOperation.NEW_PRIMARY: {
          // TODO: check logic from ClusterFields.js:432
          const configurePayload: UniverseConfigure = {
            clusterOperation: 'CREATE',
            currentClusterType: ClusterType.PRIMARY,
            rootCA: formData.securityConfig.rootCA,
            userAZSelected: false,
            communicationPorts: formData.dbConfig.communicationPorts,
            encryptionAtRestConfig: {
              encryptionAtRestEnabled: formData.securityConfig.enableEncryptionAtRest,
              key_op: formData.securityConfig.enableEncryptionAtRest ? 'ENABLE' : 'UNDEFINED'
            },
            extraDependencies: {
              installNodeExporter: formData.hiddenConfig.installNodeExporter
            },
            clusters: [
              {
                clusterType: ClusterType.PRIMARY,
                userIntent: {
                  universeName: formData.cloudConfig.universeName,
                  provider: formData.cloudConfig.provider?.uuid as string,
                  providerType: formData.cloudConfig.provider?.code as CloudType,
                  regionList: formData.cloudConfig.regionList,
                  numNodes: formData.cloudConfig.totalNodes,
                  replicationFactor: formData.cloudConfig.replicationFactor,
                  instanceType: formData.instanceConfig.instanceType as string,
                  deviceInfo: formData.instanceConfig.deviceInfo,
                  instanceTags: tagsToPayload(formData.instanceConfig.instanceTags),
                  assignPublicIP: formData.instanceConfig.assignPublicIP,
                  awsArnString: formData.instanceConfig.awsArnString || '',
                  ybSoftwareVersion: formData.dbConfig.ybSoftwareVersion,
                  masterGFlags: tagsToPayload(formData.dbConfig.masterGFlags),
                  tserverGFlags: tagsToPayload(formData.dbConfig.tserverGFlags),
                  enableNodeToNodeEncrypt: formData.securityConfig.enableNodeToNodeEncrypt,
                  enableClientToNodeEncrypt: formData.securityConfig.enableClientToNodeEncrypt,
                  accessKeyCode: formData.hiddenConfig.accessKeyCode,
                  enableYSQL: formData.hiddenConfig.enableYSQL,
                  useTimeSync: formData.hiddenConfig.useTimeSync
                }
              }
            ]
          };

          if (
            formData.securityConfig.enableEncryptionAtRest &&
            formData.securityConfig.kmsConfig &&
            configurePayload.encryptionAtRestConfig
          ) {
            configurePayload.encryptionAtRestConfig.kmsConfigUUID =
              formData.securityConfig.kmsConfig;
          }

          // in create mode we don't have "nodeDetailsSet", thus make initial configure call without placements to generate it
          const interimPayload = await api.universeConfigure(
            QUERY_KEY.universeConfigure,
            configurePayload
          );
          patchConfigResponse(interimPayload, 'CREATE', ClusterType.PRIMARY, 0);

          // replace placement from configure call with one provided by the user
          interimPayload.clusters[0].placementInfo = {
            cloudList: [
              {
                uuid: formData.cloudConfig.provider?.uuid as string,
                code: formData.cloudConfig.provider?.code as CloudType,
                regionList: getPlacementRegion(formData)
              }
            ]
          };

          // make one more configure call to validate payload before submitting
          const finalPayload = await api.universeConfigure(
            QUERY_KEY.universeConfigure,
            interimPayload
          );
          patchConfigResponse(finalPayload, 'CREATE', ClusterType.PRIMARY, 0);

          // now everything is ready to create universe
          await api.universeCreate(finalPayload);
          break;
        }

        case ClusterOperation.EDIT_PRIMARY: {
          if (universe && !_.isEqual(formData, originalFormData)) {
            const payload = universe.universeDetails;
            payload.clusterOperation = 'EDIT';
            payload.currentClusterType = ClusterType.PRIMARY;

            // update props allowed to edit by form values
            payload.clusters[0].userIntent.regionList = formData.cloudConfig.regionList;
            payload.clusters[0].userIntent.numNodes = formData.cloudConfig.totalNodes;
            payload.clusters[0].userIntent.instanceType = formData.instanceConfig
              .instanceType as string;
            payload.clusters[0].userIntent.instanceTags = tagsToPayload(
              formData.instanceConfig.instanceTags
            );
            payload.clusters[0].userIntent.deviceInfo = formData.instanceConfig.deviceInfo;
            payload.userAZSelected = formData.hiddenConfig.userAZSelected;

            // TODO: smth extra should happen here to update placements
            if (payload.userAZSelected) {
              payload.clusters[0].placementInfo.cloudList[0].regionList = getPlacementRegion(
                formData
              );
            }

            // submit configure call to validate the payload
            const finalPayload = await api.universeConfigure(QUERY_KEY.universeConfigure, payload);
            patchConfigResponse(finalPayload, 'EDIT', ClusterType.PRIMARY, 0);

            await api.universeEdit(payload, universe.universeUUID);
          } else {
            console.log('Nothing to update - no fields changed');
          }
          break;
        }
      }
    } catch (error) {
      console.error(error);
    } finally {
      whenMounted(() => setIsLoading(false));
      browserHistory.push('/universes');
    }
  };

  return { isLaunchingUniverse: isLoading, launchUniverse };
};
