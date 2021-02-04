let id = 0;

// Contant for toaster action
export const ADD_TOAST = 'ADD_TOAST';
export const REMOVE_TOAST = 'REMOVE_TOAST';

const createToast = (options) => {
  return {
    ...options,
    id: id++
  }
}

// Add Toast action
export function addToast(options = {}) {
  return {
    type: ADD_TOAST,
    payload: createToast(options)
  };
}

// Remove Toast action
export function removeToast(id) {
  return {
    payload: id,
    type: REMOVE_TOAST
  };
}
