import {setLoadingState, setPromiseResponse} from "../utils/PromiseUtils";
import {LIST_SUPPORT_BUNDLE, SET_LIST_SUPPORT_BUNDLE} from "../actions/supportBundle";

export const SupportBundle = (
  state = { supportBundle: [] },
  action
) => {
  switch (action.type) {
    case LIST_SUPPORT_BUNDLE:
      return setLoadingState(state, 'supportBundle', {});
    case SET_LIST_SUPPORT_BUNDLE:
      return setPromiseResponse(state, 'supportBundle', action);
    default:
      return state;
  }
};
