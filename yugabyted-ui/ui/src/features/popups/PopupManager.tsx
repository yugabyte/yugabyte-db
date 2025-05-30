import React, { FC, Component, ErrorInfo, ReactNode } from "react";
import Joyride, {
  ACTIONS,
  ORIGIN,
  CallBackProps,
} from 'react-joyride';
import { useLocation } from "react-router-dom";
import {
  getCustomComponentFromPopupKey,
  getLocalStorageJSON,
  getPlacementFromPopupLocation,
  getPopupConfig,
  getTargetFromPopupLocation,
  setLocalStorageJSON,
  shouldHideArrow,
  useMetricNameToFunctionMap,
} from "./popups";

const POPUP_ENABLED_LOCAL_STORAGE_KEY = "popup_enabled";

const POPUP_COOLDOWN_SECONDS = 86400; // 24 hours

// Checks if popup was closed permanently
function isPopupClosedPermanently(key: string): boolean {
  var localStorageObject = getLocalStorageJSON(key);
  var status = localStorageObject["status"];
  if (status === "closed_permanently") {
    return true;
  }
  return false;
}

function checkPopupQueryParams(
  currentParams: URLSearchParams,
  popupParams: {[key: string]: any[]}
): boolean {
  for (const key in popupParams) {
    const currentValue = currentParams.get(key);
    for (const index in popupParams[key]) {
        if (currentValue == popupParams[key][index]) {
            return true;
        }
    }
  }
  return false;
}

// Function for handling localStorage objects when user closes a popup
const handleJoyrideCallback = (data: CallBackProps) => {
  const { action, origin, step } = data;

  if (action === ACTIONS.CLOSE) {
    var localStorageObject = getLocalStorageJSON(step.data.key);

    if (origin === ORIGIN.BUTTON_PRIMARY) {
      // If user clicks permanent close button, record it in localStorage
      localStorageObject["status"] = "closed_permanently";
    } else {
      localStorageObject["status"] = "closed";
    }

    // Record time the popup was closed in localStorage
    localStorageObject["closed_timestamp"] = Math.floor(Date.now() / 1000).toString();
    setLocalStorageJSON(step.data.key, localStorageObject);

    // Also set status for enabled popup
    var enabledPopupLocalStorageObject = getLocalStorageJSON(POPUP_ENABLED_LOCAL_STORAGE_KEY);
    enabledPopupLocalStorageObject["status"] = "closed";
    setLocalStorageJSON(POPUP_ENABLED_LOCAL_STORAGE_KEY, enabledPopupLocalStorageObject);
  }
};

export const PopupManager: FC = ({ children }) => {
  const { pathname: path, search: search } = useLocation();
  const queryParams = new URLSearchParams(search);
  const popupConfig = getPopupConfig();

  var popupConfigObject: {[key: string]: any} = {};
  if (path in popupConfig) {
    popupConfigObject = popupConfig[path];
  }

  const { map: metricNameToFunctionMap, isLoading: isLoading } = useMetricNameToFunctionMap();

  // Enable a single eligible popup to be displayed for up to 24 hours.
  // This selection is persisted to localStorage so user can only see this one popup for the
  // next 24 hours.

  // If a popup is already enabled, check if it was selected 24 hours ago.
  // If so, replace it.
  var enabledPopupLocalStorageObject = getLocalStorageJSON(POPUP_ENABLED_LOCAL_STORAGE_KEY);
  // Unique popup key
  var enabledPopupKey = enabledPopupLocalStorageObject["key"];
  // Time when popup was selected
  var enabledPopupTimestamp = enabledPopupLocalStorageObject["timestamp"];
  // Whether the popup was closed by user
  var enabledPopupStatus = enabledPopupLocalStorageObject["status"];

  var shouldEnableNewPopup = false;

  if (!isNaN(enabledPopupTimestamp)) {
    shouldEnableNewPopup =
      Number(enabledPopupTimestamp) + POPUP_COOLDOWN_SECONDS < Math.floor(Date.now() / 1000);
  } else {
    shouldEnableNewPopup = true;
  }

  if (!shouldEnableNewPopup) {
    // Check if old popup was already closed. If so, don't show anything
    if (enabledPopupStatus !== "open") {
        return <>{children}</>;
    }
  } else {
    // If the user never closed the popup, mark it as closed here if it was open
    if (enabledPopupKey !== undefined) {
      var oldLocalStorageObject = getLocalStorageJSON(enabledPopupKey);
      if (oldLocalStorageObject["status"] === "open") {
        oldLocalStorageObject["status"] = "closed";
        oldLocalStorageObject["closed_timestamp"] = Math.floor(Date.now() / 1000).toString();
        setLocalStorageJSON(enabledPopupKey, oldLocalStorageObject);
      }
    }
    enabledPopupKey = undefined;
    enabledPopupTimestamp = undefined;
    enabledPopupStatus = undefined;
    // Clear localStorage of old enabled popup
    localStorage.removeItem(POPUP_ENABLED_LOCAL_STORAGE_KEY);
  }

  // Don't try to show popups until metrics are all loaded
  if (isLoading) {
    return <>{children}</>;
  }

  // Loop through all popups in config to get eligible ones.
  // If enabledPopupKey !== undefined then only that popup may be eligible.
  var eligiblePopups = [];
  var priority = Number.NEGATIVE_INFINITY;
  for (const popupLocation in popupConfigObject) {
    const popups = popupConfigObject[popupLocation];
    for (const popup of popups) {
      if (enabledPopupKey !== undefined && enabledPopupKey !== popup.key) {
        continue;
      }
      var popupMetricNames = popup.metric_names.split(",");
      var popupMetricValues = popup.metric_values.split(",");
      // Make sure all metrics are satisfied
      var metricsSatisfied = true;
      for (let i = 0; i < Math.min(popupMetricNames.length, popupMetricValues.length); i++) {
        metricsSatisfied = metricsSatisfied &&
          (popupMetricNames[i] in metricNameToFunctionMap &&
          metricNameToFunctionMap[popupMetricNames[i]](popupMetricValues[i]))
      }
      if (metricsSatisfied &&
        checkPopupQueryParams(queryParams, popup.query_parameters) &&
        !isPopupClosedPermanently(popup.key)) {
        if (popup.priority > priority) {
          eligiblePopups = [];
          priority = popup.priority
        }
        if (popup.priority >= priority) {
          eligiblePopups.push({
            target: getTargetFromPopupLocation(popupLocation),
            placement: getPlacementFromPopupLocation(popupLocation),
            content: popup.message_text,
            title: popup.message_title,
            data: {
              location: popupLocation,
              key: popup.key,
              link: popup.link,
              link_text: popup.link_text,
            },
            disableBeacon: true,
            hideBackButton: true,
            disableOverlay: true,
            disableScrolling: true,
          });
        }
      }
    }
  }
  if (eligiblePopups.length == 0) {
    return <>{children}</>;
  }
  // Get a random popup
  var popupIndex = Math.floor(Math.random() * eligiblePopups.length);
  var enabledPopup = eligiblePopups[popupIndex];

  // Found a popup to be displayed. Set localStorage values
  var localStorageObject = getLocalStorageJSON(enabledPopup.data.key);
  localStorageObject["status"] = "open";
  localStorageObject["closed_timestamp"] = undefined;
  setLocalStorageJSON(enabledPopup.data.key, localStorageObject);

  if (shouldEnableNewPopup) {
    enabledPopupLocalStorageObject["key"] = enabledPopup.data.key;
    enabledPopupLocalStorageObject["timestamp"] = Math.floor(Date.now() / 1000).toString();
    enabledPopupLocalStorageObject["status"] = "open";
    setLocalStorageJSON(POPUP_ENABLED_LOCAL_STORAGE_KEY, enabledPopupLocalStorageObject);
  }

  return (<>
    <Joyride
      steps={[enabledPopup]}
      tooltipComponent={getCustomComponentFromPopupKey(enabledPopup.data.key)}
      callback={handleJoyrideCallback}
      floaterProps={{
        hideArrow: shouldHideArrow(enabledPopup.data.location),
      }}
      styles={{
        options: {
          zIndex: 1500,
        },
      }}/>
    {children}
  </>);
};

interface ErrorBoundaryProps {
  children: ReactNode;
}

interface ErrorBoundaryState {
  hasError: boolean;
}

// Prevent PopupManager crashes from crashing the entire UI
export class PopupManagerSafe extends Component<ErrorBoundaryProps, ErrorBoundaryState> {
  constructor(props: ErrorBoundaryProps) {
    super(props);
    this.state = { hasError: false };
  }

  static getDerivedStateFromError(_: Error): ErrorBoundaryState {
    // Update state to indicate an error has occurred
    return { hasError: true };
  }

  componentDidCatch(error: Error, errorInfo: ErrorInfo): void {
    // You can log the error to an error reporting service
    console.error('Caught error:', error, errorInfo);
  }

  render(): ReactNode {
    if (this.state.hasError) {
      // You can render any custom fallback UI
      return this.props.children;
    }

    return (
      <PopupManager>
        {this.props.children}
      </PopupManager>
    );
  }
}
