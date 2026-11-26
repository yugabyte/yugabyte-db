import * as Yup from 'yup';
import { NodeAvailabilityProps } from './dtos';
import {
  ResilienceAndRegionsProps,
  ResilienceFormMode
} from '../resilence-regions/dtos';
import { getValidationMetrics, validateCommonRules } from './validation/common';
import { validateExpertRules } from './validation/expert';
import { validateGuidedRules } from './validation/guided';
import { OnPremValidationContext } from './validation/onPrem';
import { AVAILABILITY_ZONES_BY_PROVIDER } from '../../fields/FieldNames';
import { flattenAvailabilityZonesByProvider } from '../../helpers/nodesByProvider';

function resolveAvailabilityZones(parent: NodeAvailabilityProps) {
  const byProvider = parent[AVAILABILITY_ZONES_BY_PROVIDER];
  if (byProvider && Object.keys(byProvider).length > 0) {
    return flattenAvailabilityZonesByProvider(byProvider);
  }
  return parent.availabilityZones ?? {};
}

export const NodesAvailabilitySchema = (
  resilienceAndRegionsProps?: ResilienceAndRegionsProps,
  onPremContext?: OnPremValidationContext
) => {
  return Yup.object<NodeAvailabilityProps>({
    lesserNodes: Yup.number().test('availabilityZones', 'Error', function () {
      const { path, createError } = this;
      const parent = this.parent as NodeAvailabilityProps;
      const { useDedicatedNodes } = parent;
      const availabilityZones = resolveAvailabilityZones(parent);
      const metrics = getValidationMetrics(availabilityZones, resilienceAndRegionsProps);
      const fieldErrors: Yup.ValidationError[] = [];

      fieldErrors.push(
        ...validateCommonRules({
          availabilityZones,
          path,
          createError,
          onPremContext,
          resilienceAndRegionsProps
        })
      );

      if (resilienceAndRegionsProps?.resilienceFormMode === ResilienceFormMode.EXPERT_MODE) {
        fieldErrors.push(
          ...validateExpertRules({
            availabilityZones,
            path,
            createError,
            onPremContext,
            resilienceAndRegionsProps,
            metrics,
            replicationFactor:
              parent.replicationFactor ??
              resilienceAndRegionsProps?.resilienceFactor ??
              1
          })
        );
      } else {
        fieldErrors.push(
          ...validateGuidedRules({
            availabilityZones,
            path,
            createError,
            onPremContext,
            resilienceAndRegionsProps,
            metrics,
            useDedicatedNodes
          })
        );
      }

      const error = new Yup.ValidationError(
        fieldErrors.map((e) => e.message),
        'errors',
        path
      );

      error.inner = fieldErrors;

      if (fieldErrors.length > 0) {
        throw error;
      }
      return true;
    }),
    nodeCountPerAz: Yup.number()
  } as any);
};
