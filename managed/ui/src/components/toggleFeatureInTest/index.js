import { useDispatch, useSelector } from 'react-redux';
import { getFeatureFromReleased, getFeatureFromTest } from '../../selector/feature';
import { YBToggle } from '../common/forms/fields';
import { toggleFeature } from '../../actions/feature';
import './toggleFeatureIntest.scss';

const ToggleFeatureInTest = () => {
  const [featuresInTest, getFeaturesInTest] = useSelector(getFeatureFromTest);
  const [featuresInReleased] = useSelector(getFeatureFromReleased);
  const featureKeys = Object.keys(featuresInTest);
  const dispatch = useDispatch();

  const handleToggle = (feature) => {
    dispatch(toggleFeature(!getFeaturesInTest(feature), feature));
  };

  return (
    <div className="features-page">
      <div>
        <h1>Features</h1>
      </div>
      <div className="feature-list">
        {featureKeys.map((feature) => {
          return (
            <div key={feature} className="feature">
              <YBToggle
                label={`${feature} ${featuresInReleased[feature] ? '(released)' : ''}`}
                checkedVal={getFeaturesInTest(feature)}
                onToggle={() => {
                  handleToggle(feature);
                }}
                name={feature}
                input={{
                  value: getFeaturesInTest(feature),
                  onChange: () => {}
                }}
              />
            </div>
          );
        })}
      </div>
    </div>
  );
};

export default ToggleFeatureInTest;
