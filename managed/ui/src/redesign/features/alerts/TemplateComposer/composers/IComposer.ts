import { MutableRefObject } from 'react';
import { IYBEditor } from '../../../../components/YBEditor/plugins';

export interface IComposerRef {
  editors: {
    subjectEditor: MutableRefObject<IYBEditor | null>;
    bodyEditor: MutableRefObject<IYBEditor | null>;
  };
}

export interface IComposer {
  onClose: () => void;
}
