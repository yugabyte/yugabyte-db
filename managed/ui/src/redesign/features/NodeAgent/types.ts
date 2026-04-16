import { NodeAgent } from '../../utils/dtos';

export interface AugmentedNodeAgent extends NodeAgent {
  statusLabel: string;
  errorLabel: string;
}
