/// <reference types="vite/client" />

interface ImportMetaEnv {
  readonly VITE_YUGAWARE_API_URL?: string;
  readonly VITE_YB_MAP_URL?: string;
  readonly VITE_TROUBLESHOOT_API_DEV_URL?: string;
  readonly VITE_TROUBLESHOOT_API_PROD_URL?: string;
  readonly VITE_GIT_COMMIT?: string;
  readonly MODE: string;
  readonly DEV: boolean;
  readonly PROD: boolean;
}

interface ImportMeta {
  readonly env: ImportMetaEnv;
}

declare module '*.svg?img' {
  const content: string;
  export default content;
}
