import { useRef, useState, useContext, useEffect } from 'react';
import _ from 'lodash';
import { useUpdateEffect } from 'react-use';
import { useQuery } from 'react-query';
import { useWatch, useFormContext } from 'react-hook-form';
import { toast } from 'react-toastify';
import { api, QUERY_KEY } from '../../../utils/api';
import { UniverseFormContext } from '../../../UniverseFormContainer';
import { createErrorMessage, getAsyncCluster, getUserIntent } from '../../../utils/helpers';
import {
  Placement,
  Cluster,
  ClusterType,
  UniverseFormData,
  PlacementRegion,
  PlacementCloud,
  PlacementAZ
} from '../../../utils/dto';
import {
  PROVIDER_FIELD,
  REGIONS_FIELD,
  PLACEMENTS_FIELD,
  TOTAL_NODES_FIELD,
  REPLICATION_FACTOR_FIELD,
  INSTANCE_TYPE_FIELD,
  DEVICE_INFO_FIELD,
  TSERVER_K8_NODE_SPEC_FIELD,
  MASTER_K8_NODE_SPEC_FIELD,
  MASTERS_IN_DEFAULT_REGION_FIELD,
  DEFAULT_REGION_FIELD,
  MASTER_PLACEMENT_FIELD,
  MASTER_DEVICE_INFO_FIELD,
  MASTER_INSTANCE_TYPE_FIELD,
  TOAST_AUTO_DISMISS_INTERVAL,
  RESET_AZ_FIELD,
  USER_AZSELECTED_FIELD,
  SPOT_INSTANCE_FIELD
} from '../../../utils/constants';
import { CloudType } from '../../../../../../helpers/dtos';
import { getPrimaryCluster } from '../../../utils/helpers';

export const getPlacementsFromCluster = (
  cluster?: Cluster,
  providerId?: string // allocated for the future, currently there's single cloud per universe only
): any[] => {
  let placements: Placement[] = [];

  if (cluster?.placementInfo?.cloudList) {
    let regions: PlacementRegion[];
    if (providerId) {
      // find exact cloud corresponding to provider ID and take its regions
      const cloud = _.find<PlacementCloud>(cluster.placementInfo.cloudList, { uuid: providerId });
      regions = cloud?.regionList || [];
    } else {
      // concat all regions for all available clouds
      regions = cluster.placementInfo.cloudList.flatMap((item) => item.regionList);
    }
    // add extra fields with parent region data
    placements = regions.flatMap<Placement>((region) => {
      return region.azList.map((zone) => ({
        ...zone,
        parentRegionId: region.uuid,
        parentRegionName: region.name,
        parentRegionCode: region.code
      }));
    });
  } else {
    console.error('Error on extracting placements from cluster', cluster);
  }

  return _.sortBy(placements, 'name');
};

export const getPlacements = (formData: UniverseFormData): PlacementRegion[] => {
  // remove gaps from placements list
  const placements: NonNullable<Placement>[] = _.cloneDeep(
    _.compact(formData.cloudConfig.placements)
  );

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

export const useGetAllZones = () => {
  const [allAZ, setAllAZ] = useState<Placement[]>([]);
  const provider = useWatch({ name: PROVIDER_FIELD });
  const regionList = useWatch({ name: REGIONS_FIELD });

  const { data: allRegions } = useQuery(
    [QUERY_KEY.getRegionsList, provider?.uuid],
    () => api.getRegionsList(provider?.uuid),
    { enabled: !!provider?.uuid } // make sure query won't run when there's no provider defined
  );

  useEffect(() => {
    const selectedRegions = new Set(regionList);
    const zones = (allRegions || [])
      .filter((region) => selectedRegions.has(region.uuid))
      .flatMap<Placement>((region: any) => {
        // add extra fields with parent region data
        return region.zones.map((zone: any) => ({
          ...zone,
          parentRegionId: region.uuid,
          parentRegionName: region.name,
          parentRegionCode: region.code
        }));
      });

    setAllAZ(_.sortBy(zones, 'name'));
  }, [allRegions, regionList]);

  return allAZ;
};

export const useGetUnusedZones = (allZones: Placement[]) => {
  const [unUsedZones, setUnUsedZones] = useState<Placement[]>([]);
  const currentPlacements = useWatch({ name: PLACEMENTS_FIELD });

  useUpdateEffect(() => {
    const currentPlacementsMap = _.keyBy(currentPlacements, 'uuid');
    const unUsed = allZones.filter((item: any) => !currentPlacementsMap[item.uuid]);
    setUnUsedZones(unUsed);
  }, [allZones, currentPlacements]);

  return unUsedZones;
};

export const useNodePlacements = (featureFlags: Record<string, any>) => {
  const [needPlacement, setNeedPlacement] = useState(false);
  const [regionsChanged, setRegionsChanged] = useState(false);
  const { setValue, getValues } = useFormContext<UniverseFormData>();
  const [
    { universeConfigureTemplate, clusterType, mode },
    { setUniverseConfigureTemplate, setUniverseResourceTemplate, setConfigureError }
  ]: any = useContext(UniverseFormContext);

  //watchers
  const provider = useWatch({ name: PROVIDER_FIELD });
  const regionList = useWatch({ name: REGIONS_FIELD });
  const totalNodes = useWatch({ name: TOTAL_NODES_FIELD });
  const replicationFactor = useWatch({ name: REPLICATION_FACTOR_FIELD });
  const instanceType = useWatch({ name: INSTANCE_TYPE_FIELD });
  const useSpotInstance = useWatch({ name: SPOT_INSTANCE_FIELD });
  // Placement is based on T-Server Device Info in case of dedicated mode
  const deviceInfo = useWatch({ name: DEVICE_INFO_FIELD });
  const defaultRegion = useWatch({ name: DEFAULT_REGION_FIELD });
  const defaultMasterRegion = useWatch({ name: MASTERS_IN_DEFAULT_REGION_FIELD });
  const masterPlacement = useWatch({ name: MASTER_PLACEMENT_FIELD });
  const masterDeviceInfo = useWatch({ name: MASTER_DEVICE_INFO_FIELD });
  const masterInstanceType = useWatch({ name: MASTER_INSTANCE_TYPE_FIELD });
  const tserverK8SNodeResourceSpec = useWatch({ name: TSERVER_K8_NODE_SPEC_FIELD });
  const masterK8SNodeResourceSpec = useWatch({ name: MASTER_K8_NODE_SPEC_FIELD });
  const resetAZ = useWatch({ name: RESET_AZ_FIELD });
  const userAZSelected = useWatch({ name: USER_AZSELECTED_FIELD });

  const cluster = clusterType === ClusterType.PRIMARY ? getPrimaryCluster(universeConfigureTemplate) : getAsyncCluster(universeConfigureTemplate);
  const prevPropsCombination = useRef({
    instanceType,
    regionList,
    totalNodes: Number(totalNodes),
    replicationFactor,
    deviceInfo,
    defaultRegion,
    defaultMasterRegion,
    masterPlacement,
    masterDeviceInfo,
    masterInstanceType,
    tserverK8SNodeResourceSpec,
    masterK8SNodeResourceSpec,
    useSpotInstance
  });

  let payload: any = {};
  const userIntent = {
    ...cluster?.userIntent,
    ...getUserIntent({ formData: getValues() }, clusterType, featureFlags)
  };

  if (universeConfigureTemplate) {
    payload = { ...universeConfigureTemplate };
    //update the cluster intent based on cluster type
    let clusterIndex = payload.clusters.findIndex(
      (cluster: Cluster) => cluster.clusterType === clusterType
    );

    //During first Async Creation
    if (clusterIndex === -1 && clusterType === ClusterType.ASYNC)
      clusterIndex =
        payload.clusters.push({
          clusterType: ClusterType.ASYNC,
          userIntent
        }) - 1;

    if (payload.clusters[clusterIndex]?.placementInfo && getValues(PLACEMENTS_FIELD)?.length)
      payload.clusters[clusterIndex].placementInfo.cloudList[0].regionList = getPlacements(
        getValues()
      );

    payload.clusters[clusterIndex].userIntent = userIntent;
    payload['regionsChanged'] = regionsChanged;
    payload['userAZSelected'] = userAZSelected;
    payload['resetAZConfig'] = resetAZ;
    payload['clusterOperation'] = mode;
    payload['currentClusterType'] = clusterType;
  } else {
    payload = {
      currentClusterType: ClusterType.PRIMARY,
      clusterOperation: mode,
      resetAZConfig: true,
      userAZSelected: false,
      clusters: [
        {
          clusterType: ClusterType.PRIMARY,
          userIntent
        }
      ]
    };
  }

  const { isFetching } = useQuery(
    [QUERY_KEY.universeConfigure, payload],
    () => api.universeConfigure(payload),
    {
      enabled:
        needPlacement &&
        totalNodes >= replicationFactor &&
        !_.isEmpty(regionList) &&
        (!_.isEmpty(instanceType) || provider?.code === CloudType.kubernetes) &&
        !_.isEmpty(deviceInfo),
      onSuccess: async (data) => {
        const cluster = _.find(data.clusters, { clusterType });
        if (resetAZ) {
          //updating previous combinations to avoid re-rendering/redundant api calls
          prevPropsCombination.current = {
            ...prevPropsCombination.current,
            totalNodes: Number(cluster?.userIntent.numNodes),
            replicationFactor: Number(cluster?.userIntent.replicationFactor)
          };
          setValue(TOTAL_NODES_FIELD, Number(cluster?.userIntent.numNodes));
          setValue(RESET_AZ_FIELD, false);
        }
        if (userAZSelected) {
          setValue(USER_AZSELECTED_FIELD, false);
        }
        const zones = getPlacementsFromCluster(cluster);
        setValue(PLACEMENTS_FIELD, _.compact(zones));
        setUniverseConfigureTemplate(data);
        setRegionsChanged(false);
        setNeedPlacement(false);
        setConfigureError(null);
        try {
          let resource = await api.universeResource(data); // set Universe resource template whenever configure is called
          setUniverseResourceTemplate(resource);
        } catch (error) {
          toast.error(createErrorMessage(error), { autoClose: TOAST_AUTO_DISMISS_INTERVAL });
        }
      },
      onError: (error) => {
        setConfigureError(createErrorMessage(error));
        toast.error(createErrorMessage(error), { autoClose: TOAST_AUTO_DISMISS_INTERVAL });
      }
    }
  );

  useEffect(() => {
    // On page load make an initial universe configure call if placementInfo is not set
    //Add RR && Create New Universe + RR flow
    if (clusterType === ClusterType.ASYNC) {
      const asyncCluster = universeConfigureTemplate.clusters.find(
        (cluster: Cluster) => cluster.clusterType === clusterType
      );
      !asyncCluster?.placementInfo && setNeedPlacement(true);
    }
  }, [clusterType]);

  useUpdateEffect(() => {
    const propsCombination = {
      instanceType,
      regionList,
      totalNodes: Number(totalNodes),
      replicationFactor,
      deviceInfo,
      defaultRegion,
      defaultMasterRegion,
      masterPlacement,
      masterDeviceInfo,
      masterInstanceType,
      tserverK8SNodeResourceSpec,
      masterK8SNodeResourceSpec,
      useSpotInstance
    };
    if (_.isEmpty(regionList)) {
      setValue(PLACEMENTS_FIELD, [], { shouldValidate: true });
      setNeedPlacement(false);
    } else if (resetAZ || userAZSelected) {
      setNeedPlacement(true);
    } else {
      const isRegionListChanged = !_.isEqual(
        prevPropsCombination.current.regionList,
        propsCombination.regionList
      );
      const needUpdate = !_.isEqual(prevPropsCombination.current, propsCombination);
      setRegionsChanged(isRegionListChanged);
      setNeedPlacement(needUpdate);
    }

    prevPropsCombination.current = propsCombination;
  }, [
    instanceType,
    regionList,
    totalNodes,
    replicationFactor,
    deviceInfo,
    masterPlacement,
    masterDeviceInfo,
    masterInstanceType,
    resetAZ,
    userAZSelected,
    tserverK8SNodeResourceSpec,
    masterK8SNodeResourceSpec,
    useSpotInstance
  ]);
  return { isLoading: isFetching };
};
