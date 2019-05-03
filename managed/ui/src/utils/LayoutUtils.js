// Copyright (c) YugaByte, Inc.
import t from 'typy';
import { browserHistory} from 'react-router';


export function getFeatureState(features, feature_name) {
  return t(features, feature_name).safeObject || "enabled";
}

export function isNonAvailable(features, feature_name) {
  const featureState =  getFeatureState(features, feature_name);
  return (featureState !== "enabled" && featureState !== "visible");
}

export function isAvailable(features, feature_name) {
  const featureState =  getFeatureState(features, feature_name);
  return (featureState === "enabled" || featureState === "visible");
}


export function isEnabled(features, feature_name) {
  const featureState =  getFeatureState(features, feature_name);
  return featureState === "enabled";
}

export function isDisabled(features, feature_name) {
  const featureState =  getFeatureState(features, feature_name);
  return featureState === "disabled";
}

export function isHidden(features, feature_name) {
  const featureState =  getFeatureState(features, feature_name);
  return featureState === "hidden";
}

export function isNotHidden(features, feature_name) {
  const featureState =  getFeatureState(features, feature_name);
  return featureState !== "hidden";
}

export function redirectHelper(layout, location) {
  return null;
}

export function getClassName(features, feature_name) {
  const featureState =  getFeatureState(features, feature_name);
  if (featureState === "disabled" || featureState === false) return "disabled";
  if (featureState === "enabled"  || featureState === "visible") return "";
  if (featureState === "hidden") return "hidden";
  return null;
}

/* In case if the dashboard page is hidden or disabled, we need to redirect to
   different landing page. This function would read the feature to get the landing_page.
 */
export function getLandingPage(features) {
  return t(features, "main.landing_page").safeObject || "/";
}

/* We check if the given feature is available if not redirect to default landing_page
*/
export function showOrRedirect(features, feature_name) {
  if (isNonAvailable(features, feature_name)) {
    browserHistory.push(getLandingPage(features));
  }
}
