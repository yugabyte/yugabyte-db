import * as Yup from 'yup';
import { NodeAvailabilityProps } from './dtos';
import {
  ResilienceAndRegionsProps,
  ResilienceFormMode
} from '../resilence-regions/dtos';
import { getValidationMetrics, validateCommonRules } from './validation/common';
import { validateExpertRules } from './validation/expert';
import { validateGuidedRules } from './validation/guided';

export const NodesAvailabilitySchema = (resilienceAndRegionsProps?: ResilienceAndRegionsProps) => {
  return Yup.object<NodeAvailabilityProps>({
    lesserNodes: Yup.number().test('availabilityZones', 'Error', function () {
      const { path, createError } = this;
      const { availabilityZones, useDedicatedNodes } = this.parent;
      const metrics = getValidationMetrics(availabilityZones, resilienceAndRegionsProps);
      const fieldErrors: Yup.ValidationError[] = [];

      fieldErrors.push(
        ...validateCommonRules({
          availabilityZones,
          path,
          createError,
          resilienceAndRegionsProps
        })
      );

      if (resilienceAndRegionsProps?.resilienceFormMode === ResilienceFormMode.EXPERT_MODE) {
        fieldErrors.push(
          ...validateExpertRules({
            availabilityZones,
            path,
            createError,
            resilienceAndRegionsProps,
            metrics,
            replicationFactor:
              (this.parent as NodeAvailabilityProps).replicationFactor ??
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
