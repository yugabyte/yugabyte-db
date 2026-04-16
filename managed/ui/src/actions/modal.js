// Universe Modal Tasks
export const OPEN_DIALOG = 'OPEN_DIALOG';
export const CLOSE_DIALOG = 'CLOSE_DIALOG';

export function openDialog(data) {
  return {
    type: OPEN_DIALOG,
    payload: data
  };
}

export function closeDialog() {
  return {
    type: CLOSE_DIALOG
  };
}
