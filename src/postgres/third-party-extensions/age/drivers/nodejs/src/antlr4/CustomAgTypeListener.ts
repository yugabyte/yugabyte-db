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

import { AgtypeListener } from './AgtypeListener'
import {
  FloatLiteralContext,
  FloatValueContext,
  IntegerValueContext,
  PairContext,
  StringValueContext
} from './AgtypeParser'
import { ParseTreeListener } from 'antlr4ts/tree/ParseTreeListener'

type MapOrArray = Map<string, any> | any[];

class CustomAgTypeListener implements AgtypeListener, ParseTreeListener {
  rootObject?: MapOrArray;
  objectInsider: MapOrArray[] = [];
  prevObject?: MapOrArray;
  lastObject?: MapOrArray;
  lastValue: any = undefined;

  mergeArrayOrObject (key: string) {
    if (this.prevObject instanceof Array) {
      this.mergeArray()
    } else {
      this.mergeObject(key)
    }
  }

  mergeArray () {
    if (this.prevObject !== undefined && this.lastObject !== undefined && this.prevObject instanceof Array) {
      this.prevObject.push(this.lastObject)
      this.lastObject = this.prevObject
      this.objectInsider.shift()
      this.prevObject = this.objectInsider[1]
    }
  }

  mergeObject (key: string) {
    if (this.prevObject !== undefined && this.lastObject !== undefined && this.prevObject instanceof Map) {
      this.prevObject.set(key, this.lastObject)
      this.lastObject = this.prevObject
      this.objectInsider.shift()
      this.prevObject = this.objectInsider[1]
    }
  }

  createNewObject () {
    const newObject = new Map()
    this.objectInsider.unshift(newObject)
    this.prevObject = this.lastObject
    this.lastObject = newObject
  }

  createNewArray () {
    const newObject: any[] = []
    this.objectInsider.unshift(newObject)
    this.prevObject = this.lastObject
    this.lastObject = newObject
  }

  pushIfArray (value: any) {
    if (this.lastObject instanceof Array) {
      this.lastObject.push(value)
      return true
    }
    return false
  }

  exitStringValue (ctx: StringValueContext): void {
    const value = this.stripQuotes(ctx.text)
    if (!this.pushIfArray(value)) {
      this.lastValue = value
    }
  }

  exitIntegerValue (ctx: IntegerValueContext): void {
    const value = parseInt(ctx.text)
    if (!this.pushIfArray(value)) {
      this.lastValue = value
    }
  }

  exitFloatValue (ctx: FloatValueContext): void {
    const value = parseFloat(ctx.text)
    if (!this.pushIfArray(value)) {
      this.lastValue = value
    }
  }

  exitTrueBoolean (): void {
    const value = true
    if (!this.pushIfArray(value)) {
      this.lastValue = value
    }
  }

  exitFalseBoolean (): void {
    const value = false
    if (!this.pushIfArray(value)) {
      this.lastValue = value
    }
  }

  exitNullValue (): void {
    const value = null
    if (!this.pushIfArray(value)) {
      this.lastValue = value
    }
  }

  exitFloatLiteral (ctx: FloatLiteralContext): void {
    const value = ctx.text
    if (!this.pushIfArray(value)) {
      this.lastValue = value
    }
  }

  enterObjectValue (): void {
    this.createNewObject()
  }

  enterArrayValue (): void {
    this.createNewArray()
  }

  exitObjectValue (): void {
    this.mergeArray()
  }

  exitPair (ctx: PairContext): void {
    const name = this.stripQuotes(ctx.STRING().text)

    if (this.lastValue !== undefined) {
      (this.lastObject as Map<string, any>).set(name, this.lastValue)
      this.lastValue = undefined
    } else {
      this.mergeArrayOrObject(name)
    }
  }

  exitAgType (): void {
    this.rootObject = this.objectInsider.shift()
  }

  stripQuotes (quotesString: string) {
    return JSON.parse(quotesString)
  }

  getResult () {
    this.objectInsider = []
    this.prevObject = undefined
    this.lastObject = undefined

    if (!this.rootObject) {
      this.rootObject = this.lastValue
    }
    this.lastValue = undefined

    return this.rootObject
  }

  enterEveryRule (): void {
  }

  exitEveryRule (): void {
  }

  visitErrorNode (): void {
  }

  visitTerminal (): void {
  }
}

export default CustomAgTypeListener
