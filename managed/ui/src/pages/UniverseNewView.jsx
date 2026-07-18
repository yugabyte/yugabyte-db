import { useSelector } from 'react-redux';
import UniverseDetail from './UniverseDetail';
import { UniverseFormContainer } from '../redesign/features/universe/universe-form/UniverseFormContainer';

const UniverseNewView = (props) => {
  const featureFlags = useSelector((state) => state.featureFlags);
  const enableNewUI = featureFlags.test.enableNewUI || featureFlags.released.enableNewUI;

  if (enableNewUI) return <UniverseFormContainer {...props} />;
  else return <UniverseDetail {...props} />;
};

export default UniverseNewView;
