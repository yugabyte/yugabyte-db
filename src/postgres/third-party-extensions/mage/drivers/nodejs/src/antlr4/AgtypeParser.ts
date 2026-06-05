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

/* eslint-disable */
// Generated from src/antlr4/Agtype.g4 by ANTLR 4.9.0-SNAPSHOT

import { ATN } from 'antlr4ts/atn/ATN'
import { ATNDeserializer } from 'antlr4ts/atn/ATNDeserializer'
import { FailedPredicateException } from 'antlr4ts/FailedPredicateException'
import { NoViableAltException } from 'antlr4ts/NoViableAltException'
import { Parser } from 'antlr4ts/Parser'
import { ParserRuleContext } from 'antlr4ts/ParserRuleContext'
import { ParserATNSimulator } from 'antlr4ts/atn/ParserATNSimulator'
import { RecognitionException } from 'antlr4ts/RecognitionException'
// import { RuleVersion } from "antlr4ts/RuleVersion";
import { TerminalNode } from 'antlr4ts/tree/TerminalNode'
import { TokenStream } from 'antlr4ts/TokenStream'
import { Vocabulary } from 'antlr4ts/Vocabulary'
import { VocabularyImpl } from 'antlr4ts/VocabularyImpl'

import * as Utils from 'antlr4ts/misc/Utils'

import { AgtypeListener } from './AgtypeListener'

export class AgtypeParser extends Parser {
    public static readonly T__0 = 1;
    public static readonly T__1 = 2;
    public static readonly T__2 = 3;
    public static readonly T__3 = 4;
    public static readonly T__4 = 5;
    public static readonly T__5 = 6;
    public static readonly T__6 = 7;
    public static readonly T__7 = 8;
    public static readonly T__8 = 9;
    public static readonly T__9 = 10;
    public static readonly T__10 = 11;
    public static readonly T__11 = 12;
    public static readonly T__12 = 13;
    public static readonly IDENT = 14;
    public static readonly STRING = 15;
    public static readonly INTEGER = 16;
    public static readonly RegularFloat = 17;
    public static readonly ExponentFloat = 18;
    public static readonly WS = 19;
    public static readonly RULE_agType = 0;
    public static readonly RULE_agValue = 1;
    public static readonly RULE_value = 2;
    public static readonly RULE_obj = 3;
    public static readonly RULE_pair = 4;
    public static readonly RULE_array = 5;
    public static readonly RULE_typeAnnotation = 6;
    public static readonly RULE_floatLiteral = 7;
    // tslint:disable:no-trailing-whitespace
    public static readonly ruleNames: string[] = [
      'agType', 'agValue', 'value', 'obj', 'pair', 'array', 'typeAnnotation',
      'floatLiteral'
    ];

    private static readonly _LITERAL_NAMES: Array<string | undefined> = [
      undefined, "'true'", "'false'", "'null'", "'{'", "','", "'}'", "':'",
      "'['", "']'", "'::'", "'-'", "'Infinity'", "'NaN'"
    ];

    private static readonly _SYMBOLIC_NAMES: Array<string | undefined> = [
      undefined, undefined, undefined, undefined, undefined, undefined, undefined,
      undefined, undefined, undefined, undefined, undefined, undefined, undefined,
      'IDENT', 'STRING', 'INTEGER', 'RegularFloat', 'ExponentFloat', 'WS'
    ];

    public static readonly VOCABULARY: Vocabulary = new VocabularyImpl(AgtypeParser._LITERAL_NAMES, AgtypeParser._SYMBOLIC_NAMES, []);

    // @Override
    // @NotNull
    public get vocabulary (): Vocabulary {
      return AgtypeParser.VOCABULARY
    }

    // tslint:enable:no-trailing-whitespace

    // @Override
    public get grammarFileName (): string {
      return 'Agtype.g4'
    }

    // @Override
    public get ruleNames (): string[] {
      return AgtypeParser.ruleNames
    }

    // @Override
    public get serializedATN (): string {
      return AgtypeParser._serializedATN
    }

    protected createFailedPredicateException (predicate?: string, message?: string): FailedPredicateException {
      return new FailedPredicateException(this, predicate, message)
    }

    constructor (input: TokenStream) {
      super(input)
      this._interp = new ParserATNSimulator(AgtypeParser._ATN, this)
    }

    // @RuleVersion(0)
    public agType (): AgTypeContext {
      const _localctx: AgTypeContext = new AgTypeContext(this._ctx, this.state)
      this.enterRule(_localctx, 0, AgtypeParser.RULE_agType)
      try {
        this.enterOuterAlt(_localctx, 1)
        {
          this.state = 16
          this.agValue()
          this.state = 17
          this.match(AgtypeParser.EOF)
        }
      } catch (re) {
        if (re instanceof RecognitionException) {
          _localctx.exception = re
          this._errHandler.reportError(this, re)
          this._errHandler.recover(this, re)
        } else {
          throw re
        }
      } finally {
        this.exitRule()
      }
      return _localctx
    }

    // @RuleVersion(0)
    public agValue (): AgValueContext {
      const _localctx: AgValueContext = new AgValueContext(this._ctx, this.state)
      this.enterRule(_localctx, 2, AgtypeParser.RULE_agValue)
      let _la: number
      try {
        this.enterOuterAlt(_localctx, 1)
        {
          this.state = 19
          this.value()
          this.state = 21
          this._errHandler.sync(this)
          _la = this._input.LA(1)
          if (_la === AgtypeParser.T__9) {
            {
              this.state = 20
              this.typeAnnotation()
            }
          }
        }
      } catch (re) {
        if (re instanceof RecognitionException) {
          _localctx.exception = re
          this._errHandler.reportError(this, re)
          this._errHandler.recover(this, re)
        } else {
          throw re
        }
      } finally {
        this.exitRule()
      }
      return _localctx
    }

    // @RuleVersion(0)
    public value (): ValueContext {
      let _localctx: ValueContext = new ValueContext(this._ctx, this.state)
      this.enterRule(_localctx, 4, AgtypeParser.RULE_value)
      try {
        this.state = 31
        this._errHandler.sync(this)
        switch (this._input.LA(1)) {
          case AgtypeParser.STRING:
            _localctx = new StringValueContext(_localctx)
            this.enterOuterAlt(_localctx, 1)
            {
              this.state = 23
              this.match(AgtypeParser.STRING)
            }
            break
          case AgtypeParser.INTEGER:
            _localctx = new IntegerValueContext(_localctx)
            this.enterOuterAlt(_localctx, 2)
            {
              this.state = 24
              this.match(AgtypeParser.INTEGER)
            }
            break
          case AgtypeParser.T__10:
          case AgtypeParser.T__11:
          case AgtypeParser.T__12:
          case AgtypeParser.RegularFloat:
          case AgtypeParser.ExponentFloat:
            _localctx = new FloatValueContext(_localctx)
            this.enterOuterAlt(_localctx, 3)
            {
              this.state = 25
              this.floatLiteral()
            }
            break
          case AgtypeParser.T__0:
            _localctx = new TrueBooleanContext(_localctx)
            this.enterOuterAlt(_localctx, 4)
            {
              this.state = 26
              this.match(AgtypeParser.T__0)
            }
            break
          case AgtypeParser.T__1:
            _localctx = new FalseBooleanContext(_localctx)
            this.enterOuterAlt(_localctx, 5)
            {
              this.state = 27
              this.match(AgtypeParser.T__1)
            }
            break
          case AgtypeParser.T__2:
            _localctx = new NullValueContext(_localctx)
            this.enterOuterAlt(_localctx, 6)
            {
              this.state = 28
              this.match(AgtypeParser.T__2)
            }
            break
          case AgtypeParser.T__3:
            _localctx = new ObjectValueContext(_localctx)
            this.enterOuterAlt(_localctx, 7)
            {
              this.state = 29
              this.obj()
            }
            break
          case AgtypeParser.T__7:
            _localctx = new ArrayValueContext(_localctx)
            this.enterOuterAlt(_localctx, 8)
            {
              this.state = 30
              this.array()
            }
            break
          default:
            throw new NoViableAltException(this)
        }
      } catch (re) {
        if (re instanceof RecognitionException) {
          _localctx.exception = re
          this._errHandler.reportError(this, re)
          this._errHandler.recover(this, re)
        } else {
          throw re
        }
      } finally {
        this.exitRule()
      }
      return _localctx
    }

    // @RuleVersion(0)
    public obj (): ObjContext {
      const _localctx: ObjContext = new ObjContext(this._ctx, this.state)
      this.enterRule(_localctx, 6, AgtypeParser.RULE_obj)
      let _la: number
      try {
        this.state = 46
        this._errHandler.sync(this)
        switch (this.interpreter.adaptivePredict(this._input, 3, this._ctx)) {
          case 1:
            this.enterOuterAlt(_localctx, 1)
            {
              this.state = 33
              this.match(AgtypeParser.T__3)
              this.state = 34
              this.pair()
              this.state = 39
              this._errHandler.sync(this)
              _la = this._input.LA(1)
              while (_la === AgtypeParser.T__4) {
                {
                  {
                    this.state = 35
                    this.match(AgtypeParser.T__4)
                    this.state = 36
                    this.pair()
                  }
                }
                this.state = 41
                this._errHandler.sync(this)
                _la = this._input.LA(1)
              }
              this.state = 42
              this.match(AgtypeParser.T__5)
            }
            break

          case 2:
            this.enterOuterAlt(_localctx, 2)
            {
              this.state = 44
              this.match(AgtypeParser.T__3)
              this.state = 45
              this.match(AgtypeParser.T__5)
            }
            break
        }
      } catch (re) {
        if (re instanceof RecognitionException) {
          _localctx.exception = re
          this._errHandler.reportError(this, re)
          this._errHandler.recover(this, re)
        } else {
          throw re
        }
      } finally {
        this.exitRule()
      }
      return _localctx
    }

    // @RuleVersion(0)
    public pair (): PairContext {
      const _localctx: PairContext = new PairContext(this._ctx, this.state)
      this.enterRule(_localctx, 8, AgtypeParser.RULE_pair)
      try {
        this.enterOuterAlt(_localctx, 1)
        {
          this.state = 48
          this.match(AgtypeParser.STRING)
          this.state = 49
          this.match(AgtypeParser.T__6)
          this.state = 50
          this.agValue()
        }
      } catch (re) {
        if (re instanceof RecognitionException) {
          _localctx.exception = re
          this._errHandler.reportError(this, re)
          this._errHandler.recover(this, re)
        } else {
          throw re
        }
      } finally {
        this.exitRule()
      }
      return _localctx
    }

    // @RuleVersion(0)
    public array (): ArrayContext {
      const _localctx: ArrayContext = new ArrayContext(this._ctx, this.state)
      this.enterRule(_localctx, 10, AgtypeParser.RULE_array)
      let _la: number
      try {
        this.state = 65
        this._errHandler.sync(this)
        switch (this.interpreter.adaptivePredict(this._input, 5, this._ctx)) {
          case 1:
            this.enterOuterAlt(_localctx, 1)
            {
              this.state = 52
              this.match(AgtypeParser.T__7)
              this.state = 53
              this.agValue()
              this.state = 58
              this._errHandler.sync(this)
              _la = this._input.LA(1)
              while (_la === AgtypeParser.T__4) {
                {
                  {
                    this.state = 54
                    this.match(AgtypeParser.T__4)
                    this.state = 55
                    this.agValue()
                  }
                }
                this.state = 60
                this._errHandler.sync(this)
                _la = this._input.LA(1)
              }
              this.state = 61
              this.match(AgtypeParser.T__8)
            }
            break

          case 2:
            this.enterOuterAlt(_localctx, 2)
            {
              this.state = 63
              this.match(AgtypeParser.T__7)
              this.state = 64
              this.match(AgtypeParser.T__8)
            }
            break
        }
      } catch (re) {
        if (re instanceof RecognitionException) {
          _localctx.exception = re
          this._errHandler.reportError(this, re)
          this._errHandler.recover(this, re)
        } else {
          throw re
        }
      } finally {
        this.exitRule()
      }
      return _localctx
    }

    // @RuleVersion(0)
    public typeAnnotation (): TypeAnnotationContext {
      const _localctx: TypeAnnotationContext = new TypeAnnotationContext(this._ctx, this.state)
      this.enterRule(_localctx, 12, AgtypeParser.RULE_typeAnnotation)
      try {
        this.enterOuterAlt(_localctx, 1)
        {
          this.state = 67
          this.match(AgtypeParser.T__9)
          this.state = 68
          this.match(AgtypeParser.IDENT)
        }
      } catch (re) {
        if (re instanceof RecognitionException) {
          _localctx.exception = re
          this._errHandler.reportError(this, re)
          this._errHandler.recover(this, re)
        } else {
          throw re
        }
      } finally {
        this.exitRule()
      }
      return _localctx
    }

    // @RuleVersion(0)
    public floatLiteral (): FloatLiteralContext {
      const _localctx: FloatLiteralContext = new FloatLiteralContext(this._ctx, this.state)
      this.enterRule(_localctx, 14, AgtypeParser.RULE_floatLiteral)
      let _la: number
      try {
        this.state = 77
        this._errHandler.sync(this)
        switch (this._input.LA(1)) {
          case AgtypeParser.RegularFloat:
            this.enterOuterAlt(_localctx, 1)
            {
              this.state = 70
              this.match(AgtypeParser.RegularFloat)
            }
            break
          case AgtypeParser.ExponentFloat:
            this.enterOuterAlt(_localctx, 2)
            {
              this.state = 71
              this.match(AgtypeParser.ExponentFloat)
            }
            break
          case AgtypeParser.T__10:
          case AgtypeParser.T__11:
            this.enterOuterAlt(_localctx, 3)
            {
              this.state = 73
              this._errHandler.sync(this)
              _la = this._input.LA(1)
              if (_la === AgtypeParser.T__10) {
                {
                  this.state = 72
                  this.match(AgtypeParser.T__10)
                }
              }

              this.state = 75
              this.match(AgtypeParser.T__11)
            }
            break
          case AgtypeParser.T__12:
            this.enterOuterAlt(_localctx, 4)
            {
              this.state = 76
              this.match(AgtypeParser.T__12)
            }
            break
          default:
            throw new NoViableAltException(this)
        }
      } catch (re) {
        if (re instanceof RecognitionException) {
          _localctx.exception = re
          this._errHandler.reportError(this, re)
          this._errHandler.recover(this, re)
        } else {
          throw re
        }
      } finally {
        this.exitRule()
      }
      return _localctx
    }

    public static readonly _serializedATN: string =
        '\x03\uC91D\uCABA\u058D\uAFBA\u4F53\u0607\uEA8B\uC241\x03\x15R\x04\x02' +
        '\t\x02\x04\x03\t\x03\x04\x04\t\x04\x04\x05\t\x05\x04\x06\t\x06\x04\x07' +
        '\t\x07\x04\b\t\b\x04\t\t\t\x03\x02\x03\x02\x03\x02\x03\x03\x03\x03\x05' +
        '\x03\x18\n\x03\x03\x04\x03\x04\x03\x04\x03\x04\x03\x04\x03\x04\x03\x04' +
        '\x03\x04\x05\x04"\n\x04\x03\x05\x03\x05\x03\x05\x03\x05\x07\x05(\n\x05' +
        '\f\x05\x0E\x05+\v\x05\x03\x05\x03\x05\x03\x05\x03\x05\x05\x051\n\x05\x03' +
        '\x06\x03\x06\x03\x06\x03\x06\x03\x07\x03\x07\x03\x07\x03\x07\x07\x07;' +
        '\n\x07\f\x07\x0E\x07>\v\x07\x03\x07\x03\x07\x03\x07\x03\x07\x05\x07D\n' +
        '\x07\x03\b\x03\b\x03\b\x03\t\x03\t\x03\t\x05\tL\n\t\x03\t\x03\t\x05\t' +
        'P\n\t\x03\t\x02\x02\x02\n\x02\x02\x04\x02\x06\x02\b\x02\n\x02\f\x02\x0E' +
        '\x02\x10\x02\x02\x02\x02Y\x02\x12\x03\x02\x02\x02\x04\x15\x03\x02\x02' +
        '\x02\x06!\x03\x02\x02\x02\b0\x03\x02\x02\x02\n2\x03\x02\x02\x02\fC\x03' +
        '\x02\x02\x02\x0EE\x03\x02\x02\x02\x10O\x03\x02\x02\x02\x12\x13\x05\x04' +
        '\x03\x02\x13\x14\x07\x02\x02\x03\x14\x03\x03\x02\x02\x02\x15\x17\x05\x06' +
        '\x04\x02\x16\x18\x05\x0E\b\x02\x17\x16\x03\x02\x02\x02\x17\x18\x03\x02' +
        '\x02\x02\x18\x05\x03\x02\x02\x02\x19"\x07\x11\x02\x02\x1A"\x07\x12\x02' +
        '\x02\x1B"\x05\x10\t\x02\x1C"\x07\x03\x02\x02\x1D"\x07\x04\x02\x02\x1E' +
        '"\x07\x05\x02\x02\x1F"\x05\b\x05\x02 "\x05\f\x07\x02!\x19\x03\x02\x02' +
        '\x02!\x1A\x03\x02\x02\x02!\x1B\x03\x02\x02\x02!\x1C\x03\x02\x02\x02!\x1D' +
        '\x03\x02\x02\x02!\x1E\x03\x02\x02\x02!\x1F\x03\x02\x02\x02! \x03\x02\x02' +
        '\x02"\x07\x03\x02\x02\x02#$\x07\x06\x02\x02$)\x05\n\x06\x02%&\x07\x07' +
        "\x02\x02&(\x05\n\x06\x02\'%\x03\x02\x02\x02(+\x03\x02\x02\x02)\'\x03\x02" +
        '\x02\x02)*\x03\x02\x02\x02*,\x03\x02\x02\x02+)\x03\x02\x02\x02,-\x07\b' +
        '\x02\x02-1\x03\x02\x02\x02./\x07\x06\x02\x02/1\x07\b\x02\x020#\x03\x02' +
        '\x02\x020.\x03\x02\x02\x021\t\x03\x02\x02\x0223\x07\x11\x02\x0234\x07' +
        '\t\x02\x0245\x05\x04\x03\x025\v\x03\x02\x02\x0267\x07\n\x02\x027<\x05' +
        '\x04\x03\x0289\x07\x07\x02\x029;\x05\x04\x03\x02:8\x03\x02\x02\x02;>\x03' +
        '\x02\x02\x02<:\x03\x02\x02\x02<=\x03\x02\x02\x02=?\x03\x02\x02\x02><\x03' +
        '\x02\x02\x02?@\x07\v\x02\x02@D\x03\x02\x02\x02AB\x07\n\x02\x02BD\x07\v' +
        '\x02\x02C6\x03\x02\x02\x02CA\x03\x02\x02\x02D\r\x03\x02\x02\x02EF\x07' +
        '\f\x02\x02FG\x07\x10\x02\x02G\x0F\x03\x02\x02\x02HP\x07\x13\x02\x02IP' +
        '\x07\x14\x02\x02JL\x07\r\x02\x02KJ\x03\x02\x02\x02KL\x03\x02\x02\x02L' +
        'M\x03\x02\x02\x02MP\x07\x0E\x02\x02NP\x07\x0F\x02\x02OH\x03\x02\x02\x02' +
        'OI\x03\x02\x02\x02OK\x03\x02\x02\x02ON\x03\x02\x02\x02P\x11\x03\x02\x02' +
        '\x02\n\x17!)0<CKO';

    public static __ATN: ATN;
    public static get _ATN (): ATN {
      if (!AgtypeParser.__ATN) {
        AgtypeParser.__ATN = new ATNDeserializer().deserialize(Utils.toCharArray(AgtypeParser._serializedATN))
      }

      return AgtypeParser.__ATN
    }
}

export class AgTypeContext extends ParserRuleContext {
  public agValue (): AgValueContext {
    return this.getRuleContext(0, AgValueContext)
  }

  public EOF (): TerminalNode {
    return this.getToken(AgtypeParser.EOF, 0)
  }

  constructor (parent: ParserRuleContext | undefined, invokingState: number) {
    super(parent, invokingState)
  }

  // @Override
  public get ruleIndex (): number {
    return AgtypeParser.RULE_agType
  }

  // @Override
  public enterRule (listener: AgtypeListener): void {
    if (listener.enterAgType) {
      listener.enterAgType(this)
    }
  }

  // @Override
  public exitRule (listener: AgtypeListener): void {
    if (listener.exitAgType) {
      listener.exitAgType(this)
    }
  }
}

export class AgValueContext extends ParserRuleContext {
  public value (): ValueContext {
    return this.getRuleContext(0, ValueContext)
  }

  public typeAnnotation (): TypeAnnotationContext | undefined {
    return this.tryGetRuleContext(0, TypeAnnotationContext)
  }

  constructor (parent: ParserRuleContext | undefined, invokingState: number) {
    super(parent, invokingState)
  }

  // @Override
  public get ruleIndex (): number {
    return AgtypeParser.RULE_agValue
  }

  // @Override
  public enterRule (listener: AgtypeListener): void {
    if (listener.enterAgValue) {
      listener.enterAgValue(this)
    }
  }

  // @Override
  public exitRule (listener: AgtypeListener): void {
    if (listener.exitAgValue) {
      listener.exitAgValue(this)
    }
  }
}

export class ValueContext extends ParserRuleContext {
  constructor (parent: ParserRuleContext | undefined, invokingState: number) {
    super(parent, invokingState)
  }

  // @Override
  public get ruleIndex (): number {
    return AgtypeParser.RULE_value
  }

  public copyFrom (ctx: ValueContext): void {
    super.copyFrom(ctx)
  }
}

export class StringValueContext extends ValueContext {
  public STRING (): TerminalNode {
    return this.getToken(AgtypeParser.STRING, 0)
  }

  constructor (ctx: ValueContext) {
    super(ctx.parent, ctx.invokingState)
    this.copyFrom(ctx)
  }

  // @Override
  public enterRule (listener: AgtypeListener): void {
    if (listener.enterStringValue) {
      listener.enterStringValue(this)
    }
  }

  // @Override
  public exitRule (listener: AgtypeListener): void {
    if (listener.exitStringValue) {
      listener.exitStringValue(this)
    }
  }
}

export class IntegerValueContext extends ValueContext {
  public INTEGER (): TerminalNode {
    return this.getToken(AgtypeParser.INTEGER, 0)
  }

  constructor (ctx: ValueContext) {
    super(ctx.parent, ctx.invokingState)
    this.copyFrom(ctx)
  }

  // @Override
  public enterRule (listener: AgtypeListener): void {
    if (listener.enterIntegerValue) {
      listener.enterIntegerValue(this)
    }
  }

  // @Override
  public exitRule (listener: AgtypeListener): void {
    if (listener.exitIntegerValue) {
      listener.exitIntegerValue(this)
    }
  }
}

export class FloatValueContext extends ValueContext {
  public floatLiteral (): FloatLiteralContext {
    return this.getRuleContext(0, FloatLiteralContext)
  }

  constructor (ctx: ValueContext) {
    super(ctx.parent, ctx.invokingState)
    this.copyFrom(ctx)
  }

  // @Override
  public enterRule (listener: AgtypeListener): void {
    if (listener.enterFloatValue) {
      listener.enterFloatValue(this)
    }
  }

  // @Override
  public exitRule (listener: AgtypeListener): void {
    if (listener.exitFloatValue) {
      listener.exitFloatValue(this)
    }
  }
}

export class TrueBooleanContext extends ValueContext {
  constructor (ctx: ValueContext) {
    super(ctx.parent, ctx.invokingState)
    this.copyFrom(ctx)
  }

  // @Override
  public enterRule (listener: AgtypeListener): void {
    if (listener.enterTrueBoolean) {
      listener.enterTrueBoolean(this)
    }
  }

  // @Override
  public exitRule (listener: AgtypeListener): void {
    if (listener.exitTrueBoolean) {
      listener.exitTrueBoolean(this)
    }
  }
}

export class FalseBooleanContext extends ValueContext {
  constructor (ctx: ValueContext) {
    super(ctx.parent, ctx.invokingState)
    this.copyFrom(ctx)
  }

  // @Override
  public enterRule (listener: AgtypeListener): void {
    if (listener.enterFalseBoolean) {
      listener.enterFalseBoolean(this)
    }
  }

  // @Override
  public exitRule (listener: AgtypeListener): void {
    if (listener.exitFalseBoolean) {
      listener.exitFalseBoolean(this)
    }
  }
}

export class NullValueContext extends ValueContext {
  constructor (ctx: ValueContext) {
    super(ctx.parent, ctx.invokingState)
    this.copyFrom(ctx)
  }

  // @Override
  public enterRule (listener: AgtypeListener): void {
    if (listener.enterNullValue) {
      listener.enterNullValue(this)
    }
  }

  // @Override
  public exitRule (listener: AgtypeListener): void {
    if (listener.exitNullValue) {
      listener.exitNullValue(this)
    }
  }
}

export class ObjectValueContext extends ValueContext {
  public obj (): ObjContext {
    return this.getRuleContext(0, ObjContext)
  }

  constructor (ctx: ValueContext) {
    super(ctx.parent, ctx.invokingState)
    this.copyFrom(ctx)
  }

  // @Override
  public enterRule (listener: AgtypeListener): void {
    if (listener.enterObjectValue) {
      listener.enterObjectValue(this)
    }
  }

  // @Override
  public exitRule (listener: AgtypeListener): void {
    if (listener.exitObjectValue) {
      listener.exitObjectValue(this)
    }
  }
}

export class ArrayValueContext extends ValueContext {
  public array (): ArrayContext {
    return this.getRuleContext(0, ArrayContext)
  }

  constructor (ctx: ValueContext) {
    super(ctx.parent, ctx.invokingState)
    this.copyFrom(ctx)
  }

  // @Override
  public enterRule (listener: AgtypeListener): void {
    if (listener.enterArrayValue) {
      listener.enterArrayValue(this)
    }
  }

  // @Override
  public exitRule (listener: AgtypeListener): void {
    if (listener.exitArrayValue) {
      listener.exitArrayValue(this)
    }
  }
}

export class ObjContext extends ParserRuleContext {
  public pair(): PairContext[];
  public pair(i: number): PairContext;
  public pair (i?: number): PairContext | PairContext[] {
    if (i === undefined) {
      return this.getRuleContexts(PairContext)
    } else {
      return this.getRuleContext(i, PairContext)
    }
  }

  constructor (parent: ParserRuleContext | undefined, invokingState: number) {
    super(parent, invokingState)
  }

  // @Override
  public get ruleIndex (): number {
    return AgtypeParser.RULE_obj
  }

  // @Override
  public enterRule (listener: AgtypeListener): void {
    if (listener.enterObj) {
      listener.enterObj(this)
    }
  }

  // @Override
  public exitRule (listener: AgtypeListener): void {
    if (listener.exitObj) {
      listener.exitObj(this)
    }
  }
}

export class PairContext extends ParserRuleContext {
  public STRING (): TerminalNode {
    return this.getToken(AgtypeParser.STRING, 0)
  }

  public agValue (): AgValueContext {
    return this.getRuleContext(0, AgValueContext)
  }

  constructor (parent: ParserRuleContext | undefined, invokingState: number) {
    super(parent, invokingState)
  }

  // @Override
  public get ruleIndex (): number {
    return AgtypeParser.RULE_pair
  }

  // @Override
  public enterRule (listener: AgtypeListener): void {
    if (listener.enterPair) {
      listener.enterPair(this)
    }
  }

  // @Override
  public exitRule (listener: AgtypeListener): void {
    if (listener.exitPair) {
      listener.exitPair(this)
    }
  }
}

export class ArrayContext extends ParserRuleContext {
  public agValue(): AgValueContext[];
  public agValue(i: number): AgValueContext;
  public agValue (i?: number): AgValueContext | AgValueContext[] {
    if (i === undefined) {
      return this.getRuleContexts(AgValueContext)
    } else {
      return this.getRuleContext(i, AgValueContext)
    }
  }

  constructor (parent: ParserRuleContext | undefined, invokingState: number) {
    super(parent, invokingState)
  }

  // @Override
  public get ruleIndex (): number {
    return AgtypeParser.RULE_array
  }

  // @Override
  public enterRule (listener: AgtypeListener): void {
    if (listener.enterArray) {
      listener.enterArray(this)
    }
  }

  // @Override
  public exitRule (listener: AgtypeListener): void {
    if (listener.exitArray) {
      listener.exitArray(this)
    }
  }
}

export class TypeAnnotationContext extends ParserRuleContext {
  public IDENT (): TerminalNode {
    return this.getToken(AgtypeParser.IDENT, 0)
  }

  constructor (parent: ParserRuleContext | undefined, invokingState: number) {
    super(parent, invokingState)
  }

  // @Override
  public get ruleIndex (): number {
    return AgtypeParser.RULE_typeAnnotation
  }

  // @Override
  public enterRule (listener: AgtypeListener): void {
    if (listener.enterTypeAnnotation) {
      listener.enterTypeAnnotation(this)
    }
  }

  // @Override
  public exitRule (listener: AgtypeListener): void {
    if (listener.exitTypeAnnotation) {
      listener.exitTypeAnnotation(this)
    }
  }
}

export class FloatLiteralContext extends ParserRuleContext {
  public RegularFloat (): TerminalNode | undefined {
    return this.tryGetToken(AgtypeParser.RegularFloat, 0)
  }

  public ExponentFloat (): TerminalNode | undefined {
    return this.tryGetToken(AgtypeParser.ExponentFloat, 0)
  }

  constructor (parent: ParserRuleContext | undefined, invokingState: number) {
    super(parent, invokingState)
  }

  // @Override
  public get ruleIndex (): number {
    return AgtypeParser.RULE_floatLiteral
  }

  // @Override
  public enterRule (listener: AgtypeListener): void {
    if (listener.enterFloatLiteral) {
      listener.enterFloatLiteral(this)
    }
  }

  // @Override
  public exitRule (listener: AgtypeListener): void {
    if (listener.exitFloatLiteral) {
      listener.exitFloatLiteral(this)
    }
  }
}
