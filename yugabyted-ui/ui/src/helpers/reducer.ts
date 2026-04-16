export const OPEN_CLOUD_SHELL_MODAL = 'OPEN_CLOUD_SHELL_MODAL';
export const CLOSE_CLOUD_SHELL_MODAL = 'CLOSE_CLOUD_SHELL_MODAL';
export const OPEN_ALLOW_LIST_SIDE_PANEL = 'OPEN_ALLOW_LIST_SIDE_PANEL';
export const CLOSE_ALLOW_LIST_SIDE_PANEL = 'CLOSE_ALLOW_LIST_SIDE_PANEL';
export const OPEN_EDIT_INFRASTRUCTURE_MODAL = 'OPEN_EDIT_INFRASTRUCTURE_MODAL';
export const CLOSE_EDIT_INFRASTRUCTURE_MODAL = 'CLOSE_EDIT_INFRASTRUCTURE_MODAL';

export const initialState = {
  showCloudShellModal: false
};

export const clusterReducer = (
  state: Record<string, unknown>,
  action: Record<string, string>
): Record<string, unknown> => {
  switch (action.type) {
    case OPEN_CLOUD_SHELL_MODAL:
      return {
        ...state,
        showCloudShellModal: true
      };
    case CLOSE_CLOUD_SHELL_MODAL:
      return {
        ...state,
        showCloudShellModal: false
      };
    case OPEN_ALLOW_LIST_SIDE_PANEL:
      return {
        ...state,
        allowListOpen: true
      };
    case CLOSE_ALLOW_LIST_SIDE_PANEL:
      return {
        ...state,
        allowListOpen: false
      };
    case OPEN_EDIT_INFRASTRUCTURE_MODAL:
      return {
        ...state,
        editInfraModalOpen: true
      };
    case CLOSE_EDIT_INFRASTRUCTURE_MODAL:
      return {
        ...state,
        editInfraModalOpen: false
      };
    default:
      throw new Error();
  }
};
