import { Component } from 'react';
import { DiffComponentProps } from '../dtos';

// Base class for all the diff components.
export abstract class BaseDiff<P extends DiffComponentProps, S> extends Component<P, S> {
  // Get the title for the modal.
  abstract getModalTitle(): string;

  // Get the diff component to render.
  abstract getDiffComponent(): React.ReactElement;
}
