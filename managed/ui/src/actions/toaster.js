export const ADD_TOAST = 'ADD_TOAST';
export const REMOVE_TOAST = 'REMOVE_TOAST';

let id = 0;


const createToast = (options) => {
  return {
    ...options,
    id: id++
  }
}

export function addToast(options = {}) {
  return {
    type: ADD_TOAST,
    payload: createToast(options)
  };
}

export function removeToast(id) {
  return {
    payload: id,
    type: REMOVE_TOAST
  };
}
