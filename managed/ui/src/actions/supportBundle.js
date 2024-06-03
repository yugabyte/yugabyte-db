

export const LIST_SUPPORT_BUNDLE = 'SUPPORT/BUNDLE/GET';
export const SET_LIST_SUPPORT_BUNDLE = 'SET/SUPPORT/BUNDLE/GET';
export const CREATE_SUPPORT_BUNDLE = 'SUPPORT/BUNDLE/POST';

export function createSupportBundle(payload) {
  return {
    type: CREATE_SUPPORT_BUNDLE,
    payload
  };
}

export function listSupportBundle(payload) {
  return {
    type: LIST_SUPPORT_BUNDLE,
    payload
  };
}

export function setListSupportBundle(response) {
  return {
    type: SET_LIST_SUPPORT_BUNDLE,
    payload: response
  };
}
