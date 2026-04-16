/**
 * NODE_LEVEL node-count validation attaches to the synthetic `lesserNodes` field.
 */
export const LESS_NODES_ERROR_MESSAGES = [
  'errMsg.lessNodes',
  'errMsg.lessNodesDedicated'
] as const;

export function shouldMarkNodesFieldError(
  showErrorsAfterSubmit: boolean,
  lesserNodesMessage: string | undefined
): boolean {
  return (
    showErrorsAfterSubmit &&
    !!lesserNodesMessage &&
    (LESS_NODES_ERROR_MESSAGES as readonly string[]).includes(lesserNodesMessage)
  );
}
