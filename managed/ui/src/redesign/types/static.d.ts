declare module '*.module.css' {
  const classes: { [key: string]: string };
  export default classes;
}
declare module '*.module.scss' {
  const classes: { [key: string]: string };
  export default classes;
}
declare module '*.module.sass' {
  const classes: { [key: string]: string };
  export default classes;
}
declare module '*.module.less' {
  const classes: { [key: string]: string };
  export default classes;
}
declare module '*.module.styl' {
  const classes: { [key: string]: string };
  export default classes;
}

declare module '*.css';
declare module '*.scss';
declare module '*.sass';
declare module '*.less';
declare module '*.styl';

// Type as defined here: https://github.com/jaredLunde/snowpack-plugin-svgr
// declare module '*.svg' {
//   const ref: React.RefForwardingComponent<SVGSVGElement, React.SVGAttributes<SVGSVGElement>>;
//   export default ref;
// }

// declare module '*.svg' {
//   const ref: string;
//   export default ref;
// }

// declare module '*.svg' {
//   import * as React from 'react';

//   const ReactComponent: React.FunctionComponent<React.SVGProps<SVGSVGElement> & { title?: string }>;

//   export default ReactComponent;
// }

declare module '*.bmp' {
  const ref: string;
  export default ref;
}
declare module '*.gif' {
  const ref: string;
  export default ref;
}
declare module '*.jpg' {
  const ref: string;
  export default ref;
}
declare module '*.jpeg' {
  const ref: string;
  export default ref;
}
declare module '*.png' {
  const ref: string;
  export default ref;
}
