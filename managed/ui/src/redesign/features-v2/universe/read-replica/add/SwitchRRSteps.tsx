import { useContext, forwardRef, useImperativeHandle, useRef } from 'react';
import { useMap } from 'react-use';
import { AddRRContextMethods, AddRRContext, AddReadReplicaSteps } from './AddReadReplicaContext';
import {
  RRRegionsAndAZ,
  RRDatabaseSettings,
  RRInstanceSettings,
  RRReviewAndSummary
} from './steps';
import { StepsRef } from './AddReadReplicaContext';

export const SwitchRRSteps = forwardRef((_props, forwardRef) => {
  const [{ activeStep }] = (useContext(AddRRContext) as unknown) as AddRRContextMethods;

  const currentComponentRef = useRef<StepsRef>(null);
  const mappings = {
    [AddReadReplicaSteps.REGIONS_AND_AZ]: <RRRegionsAndAZ ref={currentComponentRef} />,
    [AddReadReplicaSteps.INSTANCE]: <RRInstanceSettings ref={currentComponentRef} />,
    [AddReadReplicaSteps.DATABASE]: <RRDatabaseSettings ref={currentComponentRef} />,
    [AddReadReplicaSteps.REVIEW]: <RRReviewAndSummary ref={currentComponentRef} />
  };

  const [, { get }] = useMap<Record<number, JSX.Element>>(mappings);
  useImperativeHandle(forwardRef, () => currentComponentRef.current, [
    currentComponentRef.current,
    activeStep
  ]);

  const getCurrentComponent = () => {
    return get(activeStep);
  };

  return getCurrentComponent() ?? null;
});
