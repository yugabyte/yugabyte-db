import * as Yup from 'yup';
import { TFunction } from 'i18next';
import { NodeAvailabilityProps } from './dtos';
import { FaultToleranceType, ResilienceAndRegionsProps } from '../resilence-regions/dtos';
import { getFaultToleranceNeededForAZ, getNodeCount } from '../../CreateUniverseUtils';

export const NodesAvailabilitySchema = (t: TFunction, resilienceAndRegionsProps?: ResilienceAndRegionsProps) => {

    return Yup.object<NodeAvailabilityProps>({
        lesserNodes: Yup.number().test('availabilityZones', 'Error', function () {
            const { path, createError } = this;
            const { availabilityZones, useDedicatedNodes } = this.parent;
            const nodeCounts = getNodeCount(availabilityZones);
            const faultToleranceNeeded = getFaultToleranceNeededForAZ(resilienceAndRegionsProps?.replicationFactor ?? 1);
            if (resilienceAndRegionsProps?.faultToleranceType === FaultToleranceType.NODE_LEVEL && nodeCounts < faultToleranceNeeded) {
                return createError({ path, message: t( useDedicatedNodes ? 'errMsg.lessNodesDedicated' : 'errMsg.lessNodes', { nodeCount: nodeCounts, faultToleranceNeeded: faultToleranceNeeded }) });
            }
            return true;
        })
    } as any);
};
