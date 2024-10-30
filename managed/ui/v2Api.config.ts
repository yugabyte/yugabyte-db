import { defineConfig } from 'orval';

const inputYamlFile = '../src/main/resources/openapi.yaml';
const targetWorkspace = `./src/v2/`;

const apiDir = `${targetWorkspace}/api`;


const header = (info): string[] => [
    `Do not edit manually.`,
    "",
    ...(info.title ? [info.title] : []),
    ...(info.description ? [info.description] : []),
    ...(info.version ? [`OpenAPI spec version: ${info.version}`] : []),
    "",
    "Copyright 2021 YugaByte, Inc. and Contributors",
    `Licensed under the Polyform Free Trial License 1.0.0 (the "License")`,
    `You may not use this file except in compliance with the License. You may obtain a copy of the License at`,
    `http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt`,
    "",
]

export default defineConfig({
    api: {
        output: {
            target: apiDir,
            mode: 'tags-split',
            client: 'react-query',
            clean: true,
            prettier: true,
            override: {
                useNamedParameters: true,
                query: {
                    signal: false
                },
                mutator: {
                    path: 'src/v2/helpers/mutators/YBAxios.ts',
                    name: 'YBAxiosInstance',
                },
                header
            }
        },
        input: {
            override: {
                transformer: 'src/v2/helpers/transformers/makeCUUIDOptional.js',
            },
            target: inputYamlFile,
            converterOptions: true
        }
    }
});
