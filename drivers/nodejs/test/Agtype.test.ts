/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

import { AGTypeParse } from '../src'

describe('Parsing', () => {
  it('Vertex', async () => {
    expect(
      AGTypeParse('{"id": 844424930131969, "label": "Part", "properties": {"part_num": "123", "number": 3141592653589793, "float": 3.141592653589793}}::vertex')
    ).toStrictEqual(new Map<string, any>(Object.entries({
      id: 844424930131969,
      label: 'Part',
      properties: new Map(Object.entries({
        part_num: '123',
        number: 3141592653589793,
        float: 3.141592653589793
      }))
    })))
  })

  it('Edge', async () => {
    expect(
      AGTypeParse('{"id": 1125899906842625, "label": "used_by", "end_id": 844424930131970, "start_id": 844424930131969, "properties": {"quantity": 1}}::edge')
    ).toStrictEqual(new Map(Object.entries({
      id: 1125899906842625,
      label: 'used_by',
      end_id: 844424930131970,
      start_id: 844424930131969,
      properties: new Map(Object.entries({ quantity: 1 }))
    })))
  })

  it('Path', async () => {
    expect(
      AGTypeParse('[{"id": 844424930131969, "label": "Part", "properties": {"part_num": "123"}}::vertex, {"id": 1125899906842625, "label": "used_by", "end_id": 844424930131970, "start_id": 844424930131969, "properties": {"quantity": 1}}::edge, {"id": 844424930131970, "label": "Part", "properties": {"part_num": "345"}}::vertex]::path')
    ).toStrictEqual([
      new Map(Object.entries({
        id: 844424930131969,
        label: 'Part',
        properties: new Map(Object.entries({ part_num: '123' }))
      })),
      new Map(Object.entries({
        id: 1125899906842625,
        label: 'used_by',
        end_id: 844424930131970,
        start_id: 844424930131969,
        properties: new Map(Object.entries({ quantity: 1 }))
      })),
      new Map(Object.entries({
        id: 844424930131970,
        label: 'Part',
        properties: new Map(Object.entries({ part_num: '345' }))
      }))
    ])
  })

  it('Null Properties', async () => {
    expect(
      AGTypeParse('{"id": 1688849860263937, "label": "car", "properties": {}}::vertex')
    ).toStrictEqual(new Map<string, any>(Object.entries({
      id: 1688849860263937,
      label: 'car',
      properties: new Map(Object.entries({}))
    })))
  })

  it('Nested Agtype', () => {
    expect(
      AGTypeParse('{"id": 1688849860263937, "label": "car", "properties": {"a": {"b":{"c":{"d":[1, 2, "A"]}}}}}::vertex')
    ).toStrictEqual(new Map<string, any>(Object.entries({
      id: 1688849860263937,
      label: 'car',
      properties: new Map<string, any>(Object.entries({
        a: new Map<string, any>(Object.entries({
          b: new Map<string, any>(Object.entries({
            c: new Map<string, any>(Object.entries({
              d: [
                1,
                2,
                'A'
              ]
            }))
          }))
        }))
      }))
    })))
  })
})
