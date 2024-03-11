#### create_aggregate

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="255" height="95" viewbox="0 0 255 95"><path class="connector" d="M0 22h35m174 0h31m-215 25q0 5 5 5h5m185 0h5q5 0 5-5m-210-25q5 0 5 5v50q0 5 5 5h5m149 0h41q5 0 5-5v-50q0-5 5-5m5 0h15"/><polygon points="0,29 5,22 0,15" style="fill:black;stroke-width:0"/><a xlink:href="#create-aggregate-normal"><rect class="rule" x="35" y="5" width="174" height="25"/><text class="text" x="45" y="22">create_aggregate_normal</text></a><a xlink:href="#create-aggregate-order-by"><rect class="rule" x="35" y="35" width="185" height="25"/><text class="text" x="45" y="52">create_aggregate_order_by</text></a><a xlink:href="#create-aggregate-old"><rect class="rule" x="35" y="65" width="149" height="25"/><text class="text" x="45" y="82">create_aggregate_old</text></a><polygon points="251,29 255,29 255,15 251,15" style="fill:black;stroke-width:0"/></svg>

#### create_aggregate_normal

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="830" height="160" viewbox="0 0 830 160"><path class="connector" d="M0 52h15m67 0h10m94 0h10m121 0h10m25 0h50m-5 0q-5 0-5-5v-20q0-5 5-5h46m24 0h46q5 0 5 5v20q0 5-5 5m-5 0h40m-181 0q5 0 5 5v20q0 5 5 5h5m28 0h123q5 0 5-5v-20q0-5 5-5m5 0h10m25 0h7m2 0h2m2 0h2m-598 80h2m2 0h2m2 0h7m25 0h10m61 0h10m30 0h10m52 0h10m24 0h10m57 0h10m30 0h10m118 0h50m-5 0q-5 0-5-5v-17q0-5 5-5h218q5 0 5 5v17q0 5-5 5m-189 0h10m174 0h40m-283 0q5 0 5 5v8q0 5 5 5h258q5 0 5-5v-8q0-5 5-5m5 0h10m25 0h15"/><polygon points="0,59 5,52 0,45" style="fill:black;stroke-width:0"/><rect class="literal" x="15" y="35" width="67" height="25" rx="7"/><text class="text" x="25" y="52">CREATE</text><rect class="literal" x="92" y="35" width="94" height="25" rx="7"/><text class="text" x="102" y="52">AGGREGATE</text><a xlink:href="../../../syntax_resources/grammar_diagrams#aggregate-name"><rect class="rule" x="196" y="35" width="121" height="25"/><text class="text" x="206" y="52">aggregate_name</text></a><rect class="literal" x="327" y="35" width="25" height="25" rx="7"/><text class="text" x="337" y="52">(</text><rect class="literal" x="443" y="5" width="24" height="25" rx="7"/><text class="text" x="453" y="22">,</text><a xlink:href="#aggregate-arg"><rect class="rule" x="402" y="35" width="106" height="25"/><text class="text" x="412" y="52">aggregate_arg</text></a><rect class="literal" x="382" y="65" width="28" height="25" rx="7"/><text class="text" x="392" y="82">*</text><rect class="literal" x="558" y="35" width="25" height="25" rx="7"/><text class="text" x="568" y="52">)</text><rect class="literal" x="15" y="115" width="25" height="25" rx="7"/><text class="text" x="25" y="132">(</text><rect class="literal" x="50" y="115" width="61" height="25" rx="7"/><text class="text" x="60" y="132">SFUNC</text><rect class="literal" x="121" y="115" width="30" height="25" rx="7"/><text class="text" x="131" y="132">=</text><a xlink:href="../../../syntax_resources/grammar_diagrams#sfunc"><rect class="rule" x="161" y="115" width="52" height="25"/><text class="text" x="171" y="132">sfunc</text></a><rect class="literal" x="223" y="115" width="24" height="25" rx="7"/><text class="text" x="233" y="132">,</text><rect class="literal" x="257" y="115" width="57" height="25" rx="7"/><text class="text" x="267" y="132">STYPE</text><rect class="literal" x="324" y="115" width="30" height="25" rx="7"/><text class="text" x="334" y="132">=</text><a xlink:href="../../../syntax_resources/grammar_diagrams#state-data-type"><rect class="rule" x="364" y="115" width="118" height="25"/><text class="text" x="374" y="132">state_data_type</text></a><rect class="literal" x="532" y="115" width="24" height="25" rx="7"/><text class="text" x="542" y="132">,</text><a xlink:href="#aggregate-normal-option"><rect class="rule" x="566" y="115" width="174" height="25"/><text class="text" x="576" y="132">aggregate_normal_option</text></a><rect class="literal" x="790" y="115" width="25" height="25" rx="7"/><text class="text" x="800" y="132">)</text><polygon points="826,139 830,139 830,125 826,125" style="fill:black;stroke-width:0"/></svg>

#### create_aggregate_order_by

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="871" height="145" viewbox="0 0 871 145"><path class="connector" d="M0 52h15m67 0h10m94 0h10m121 0h10m25 0h50m-5 0q-5 0-5-5v-20q0-5 5-5h46m24 0h46q5 0 5 5v20q0 5-5 5m-5 0h40m-181 0q5 0 5 5v8q0 5 5 5h156q5 0 5-5v-8q0-5 5-5m5 0h10m62 0h10m35 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h46m24 0h46q5 0 5 5v20q0 5-5 5m-5 0h30m25 0h7m2 0h2m2 0h2m-871 65h2m2 0h2m2 0h7m25 0h10m61 0h10m30 0h10m52 0h10m24 0h10m57 0h10m30 0h10m118 0h50m-5 0q-5 0-5-5v-17q0-5 5-5h229q5 0 5 5v17q0 5-5 5m-200 0h10m185 0h40m-294 0q5 0 5 5v8q0 5 5 5h269q5 0 5-5v-8q0-5 5-5m5 0h10m25 0h15"/><polygon points="0,59 5,52 0,45" style="fill:black;stroke-width:0"/><rect class="literal" x="15" y="35" width="67" height="25" rx="7"/><text class="text" x="25" y="52">CREATE</text><rect class="literal" x="92" y="35" width="94" height="25" rx="7"/><text class="text" x="102" y="52">AGGREGATE</text><a xlink:href="../../../syntax_resources/grammar_diagrams#aggregate-name"><rect class="rule" x="196" y="35" width="121" height="25"/><text class="text" x="206" y="52">aggregate_name</text></a><rect class="literal" x="327" y="35" width="25" height="25" rx="7"/><text class="text" x="337" y="52">(</text><rect class="literal" x="443" y="5" width="24" height="25" rx="7"/><text class="text" x="453" y="22">,</text><a xlink:href="#aggregate-arg"><rect class="rule" x="402" y="35" width="106" height="25"/><text class="text" x="412" y="52">aggregate_arg</text></a><rect class="literal" x="558" y="35" width="62" height="25" rx="7"/><text class="text" x="568" y="52">ORDER</text><rect class="literal" x="630" y="35" width="35" height="25" rx="7"/><text class="text" x="640" y="52">BY</text><rect class="literal" x="736" y="5" width="24" height="25" rx="7"/><text class="text" x="746" y="22">,</text><a xlink:href="#aggregate-arg"><rect class="rule" x="695" y="35" width="106" height="25"/><text class="text" x="705" y="52">aggregate_arg</text></a><rect class="literal" x="831" y="35" width="25" height="25" rx="7"/><text class="text" x="841" y="52">)</text><rect class="literal" x="15" y="100" width="25" height="25" rx="7"/><text class="text" x="25" y="117">(</text><rect class="literal" x="50" y="100" width="61" height="25" rx="7"/><text class="text" x="60" y="117">SFUNC</text><rect class="literal" x="121" y="100" width="30" height="25" rx="7"/><text class="text" x="131" y="117">=</text><a xlink:href="../../../syntax_resources/grammar_diagrams#sfunc"><rect class="rule" x="161" y="100" width="52" height="25"/><text class="text" x="171" y="117">sfunc</text></a><rect class="literal" x="223" y="100" width="24" height="25" rx="7"/><text class="text" x="233" y="117">,</text><rect class="literal" x="257" y="100" width="57" height="25" rx="7"/><text class="text" x="267" y="117">STYPE</text><rect class="literal" x="324" y="100" width="30" height="25" rx="7"/><text class="text" x="334" y="117">=</text><a xlink:href="../../../syntax_resources/grammar_diagrams#state-data-type"><rect class="rule" x="364" y="100" width="118" height="25"/><text class="text" x="374" y="117">state_data_type</text></a><rect class="literal" x="532" y="100" width="24" height="25" rx="7"/><text class="text" x="542" y="117">,</text><a xlink:href="#aggregate-order-by-option"><rect class="rule" x="566" y="100" width="185" height="25"/><text class="text" x="576" y="117">aggregate_order_by_option</text></a><rect class="literal" x="801" y="100" width="25" height="25" rx="7"/><text class="text" x="811" y="117">)</text><polygon points="837,124 841,124 841,110 837,110" style="fill:black;stroke-width:0"/></svg>

#### create_aggregate_old

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="770" height="100" viewbox="0 0 770 100"><path class="connector" d="M0 22h15m67 0h10m94 0h10m121 0h10m25 0h10m81 0h10m30 0h10m81 0h10m24 0h7m2 0h2m2 0h2m-623 50h2m2 0h2m2 0h7m61 0h10m30 0h10m52 0h10m24 0h10m57 0h10m30 0h10m118 0h50m-5 0q-5 0-5-5v-17q0-5 5-5h193q5 0 5 5v17q0 5-5 5m-164 0h10m149 0h40m-258 0q5 0 5 5v8q0 5 5 5h233q5 0 5-5v-8q0-5 5-5m5 0h10m25 0h15"/><polygon points="0,29 5,22 0,15" style="fill:black;stroke-width:0"/><rect class="literal" x="15" y="5" width="67" height="25" rx="7"/><text class="text" x="25" y="22">CREATE</text><rect class="literal" x="92" y="5" width="94" height="25" rx="7"/><text class="text" x="102" y="22">AGGREGATE</text><a xlink:href="../../../syntax_resources/grammar_diagrams#aggregate-name"><rect class="rule" x="196" y="5" width="121" height="25"/><text class="text" x="206" y="22">aggregate_name</text></a><rect class="literal" x="327" y="5" width="25" height="25" rx="7"/><text class="text" x="337" y="22">(</text><rect class="literal" x="362" y="5" width="81" height="25" rx="7"/><text class="text" x="372" y="22">BASETYPE</text><rect class="literal" x="453" y="5" width="30" height="25" rx="7"/><text class="text" x="463" y="22">=</text><a xlink:href="../../../syntax_resources/grammar_diagrams#base-type"><rect class="rule" x="493" y="5" width="81" height="25"/><text class="text" x="503" y="22">base_type</text></a><rect class="literal" x="584" y="5" width="24" height="25" rx="7"/><text class="text" x="594" y="22">,</text><rect class="literal" x="15" y="55" width="61" height="25" rx="7"/><text class="text" x="25" y="72">SFUNC</text><rect class="literal" x="86" y="55" width="30" height="25" rx="7"/><text class="text" x="96" y="72">=</text><a xlink:href="../../../syntax_resources/grammar_diagrams#sfunc"><rect class="rule" x="126" y="55" width="52" height="25"/><text class="text" x="136" y="72">sfunc</text></a><rect class="literal" x="188" y="55" width="24" height="25" rx="7"/><text class="text" x="198" y="72">,</text><rect class="literal" x="222" y="55" width="57" height="25" rx="7"/><text class="text" x="232" y="72">STYPE</text><rect class="literal" x="289" y="55" width="30" height="25" rx="7"/><text class="text" x="299" y="72">=</text><a xlink:href="../../../syntax_resources/grammar_diagrams#state-data-type"><rect class="rule" x="329" y="55" width="118" height="25"/><text class="text" x="339" y="72">state_data_type</text></a><rect class="literal" x="497" y="55" width="24" height="25" rx="7"/><text class="text" x="507" y="72">,</text><a xlink:href="#aggregate-old-option"><rect class="rule" x="531" y="55" width="149" height="25"/><text class="text" x="541" y="72">aggregate_old_option</text></a><rect class="literal" x="730" y="55" width="25" height="25" rx="7"/><text class="text" x="740" y="72">)</text><polygon points="766,79 770,79 770,65 766,65" style="fill:black;stroke-width:0"/></svg>

#### aggregate_arg

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="435" height="50" viewbox="0 0 435 50"><path class="connector" d="M0 22h35m147 0h20m-182 0q5 0 5 5v8q0 5 5 5h157q5 0 5-5v-8q0-5 5-5m5 0h30m85 0h20m-120 0q5 0 5 5v8q0 5 5 5h95q5 0 5-5v-8q0-5 5-5m5 0h10m73 0h15"/><polygon points="0,29 5,22 0,15" style="fill:black;stroke-width:0"/><a xlink:href="../../../syntax_resources/grammar_diagrams#aggregate-arg-mode"><rect class="rule" x="35" y="5" width="147" height="25"/><text class="text" x="45" y="22">aggregate_arg_mode</text></a><a xlink:href="../../../syntax_resources/grammar_diagrams#formal-arg"><rect class="rule" x="232" y="5" width="85" height="25"/><text class="text" x="242" y="22">formal_arg</text></a><a xlink:href="../../../syntax_resources/grammar_diagrams#arg-type"><rect class="rule" x="347" y="5" width="73" height="25"/><text class="text" x="357" y="22">arg_type</text></a><polygon points="431,29 435,29 435,15 431,15" style="fill:black;stroke-width:0"/></svg>

#### aggregate_normal_option

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="415" height="725" viewbox="0 0 415 725"><path class="connector" d="M0 22h35m67 0h10m30 0h10m114 0h134m-375 25q0 5 5 5h5m89 0h10m30 0h10m50 0h161q5 0 5-5m-365 30q0 5 5 5h5m136 0h214q5 0 5-5m-365 30q0 5 5 5h5m144 0h10m30 0h30m93 0h28m-131 25q0 5 5 5h5m92 0h14q5 0 5-5m-126-25q5 0 5 5v50q0 5 5 5h5m101 0h5q5 0 5-5v-50q0-5 5-5m5 0h15q5 0 5-5m-365 90q0 5 5 5h5m110 0h10m30 0h10m96 0h94q5 0 5-5m-365 30q0 5 5 5h5m97 0h10m30 0h10m77 0h126q5 0 5-5m-365 30q0 5 5 5h5m114 0h10m30 0h10m91 0h95q5 0 5-5m-365 30q0 5 5 5h5m81 0h10m30 0h10m114 0h105q5 0 5-5m-365 30q0 5 5 5h5m71 0h10m30 0h10m64 0h165q5 0 5-5m-365 30q0 5 5 5h5m85 0h10m30 0h10m76 0h139q5 0 5-5m-365 30q0 5 5 5h5m67 0h10m30 0h10m130 0h103q5 0 5-5m-365 30q0 5 5 5h5m77 0h10m30 0h10m126 0h97q5 0 5-5m-365 30q0 5 5 5h5m99 0h10m30 0h10m62 0h139q5 0 5-5m-365 30q0 5 5 5h5m146 0h204q5 0 5-5m-365 30q0 5 5 5h5m154 0h10m30 0h30m93 0h28m-131 25q0 5 5 5h5m92 0h14q5 0 5-5m-126-25q5 0 5 5v50q0 5 5 5h5m101 0h5q5 0 5-5v-50q0-5 5-5m5 0h5q5 0 5-5m-365 90q0 5 5 5h5m91 0h10m30 0h10m126 0h83q5 0 5-5m-365 30q0 5 5 5h5m68 0h10m30 0h10m101 0h131q5 0 5-5m-370-595q5 0 5 5v620q0 5 5 5h5m80 0h10m30 0h30m51 0h65m-126 25q0 5 5 5h5m96 0h5q5 0 5-5m-121-25q5 0 5 5v50q0 5 5 5h5m69 0h32q5 0 5-5v-50q0-5 5-5m5 0h84q5 0 5-5v-620q0-5 5-5m5 0h15"/><polygon points="0,29 5,22 0,15" style="fill:black;stroke-width:0"/><rect class="literal" x="35" y="5" width="67" height="25" rx="7"/><text class="text" x="45" y="22">SSPACE</text><rect class="literal" x="112" y="5" width="30" height="25" rx="7"/><text class="text" x="122" y="22">=</text><a xlink:href="../../../syntax_resources/grammar_diagrams#state-data-size"><rect class="rule" x="152" y="5" width="114" height="25"/><text class="text" x="162" y="22">state_data_size</text></a><rect class="literal" x="35" y="35" width="89" height="25" rx="7"/><text class="text" x="45" y="52">FINALFUNC</text><rect class="literal" x="134" y="35" width="30" height="25" rx="7"/><text class="text" x="144" y="52">=</text><a xlink:href="../../../syntax_resources/grammar_diagrams#ffunc"><rect class="rule" x="174" y="35" width="50" height="25"/><text class="text" x="184" y="52">ffunc</text></a><rect class="literal" x="35" y="65" width="136" height="25" rx="7"/><text class="text" x="45" y="82">FINALFUNC_EXTRA</text><rect class="literal" x="35" y="95" width="144" height="25" rx="7"/><text class="text" x="45" y="112">FINALFUNC_MODIFY</text><rect class="literal" x="189" y="95" width="30" height="25" rx="7"/><text class="text" x="199" y="112">=</text><rect class="literal" x="249" y="95" width="93" height="25" rx="7"/><text class="text" x="259" y="112">READ_ONLY</text><rect class="literal" x="249" y="125" width="92" height="25" rx="7"/><text class="text" x="259" y="142">SHAREABLE</text><rect class="literal" x="249" y="155" width="101" height="25" rx="7"/><text class="text" x="259" y="172">READ_WRITE</text><rect class="literal" x="35" y="185" width="110" height="25" rx="7"/><text class="text" x="45" y="202">COMBINEFUNC</text><rect class="literal" x="155" y="185" width="30" height="25" rx="7"/><text class="text" x="165" y="202">=</text><a xlink:href="../../../syntax_resources/grammar_diagrams#combinefunc"><rect class="rule" x="195" y="185" width="96" height="25"/><text class="text" x="205" y="202">combinefunc</text></a><rect class="literal" x="35" y="215" width="97" height="25" rx="7"/><text class="text" x="45" y="232">SERIALFUNC</text><rect class="literal" x="142" y="215" width="30" height="25" rx="7"/><text class="text" x="152" y="232">=</text><a xlink:href="../../../syntax_resources/grammar_diagrams#serialfunc"><rect class="rule" x="182" y="215" width="77" height="25"/><text class="text" x="192" y="232">serialfunc</text></a><rect class="literal" x="35" y="245" width="114" height="25" rx="7"/><text class="text" x="45" y="262">DESERIALFUNC</text><rect class="literal" x="159" y="245" width="30" height="25" rx="7"/><text class="text" x="169" y="262">=</text><a xlink:href="../../../syntax_resources/grammar_diagrams#deserialfunc"><rect class="rule" x="199" y="245" width="91" height="25"/><text class="text" x="209" y="262">deserialfunc</text></a><rect class="literal" x="35" y="275" width="81" height="25" rx="7"/><text class="text" x="45" y="292">INITCOND</text><rect class="literal" x="126" y="275" width="30" height="25" rx="7"/><text class="text" x="136" y="292">=</text><a xlink:href="../../../syntax_resources/grammar_diagrams#initial-condition"><rect class="rule" x="166" y="275" width="114" height="25"/><text class="text" x="176" y="292">initial_condition</text></a><rect class="literal" x="35" y="305" width="71" height="25" rx="7"/><text class="text" x="45" y="322">MSFUNC</text><rect class="literal" x="116" y="305" width="30" height="25" rx="7"/><text class="text" x="126" y="322">=</text><a xlink:href="../../../syntax_resources/grammar_diagrams#msfunc"><rect class="rule" x="156" y="305" width="64" height="25"/><text class="text" x="166" y="322">msfunc</text></a><rect class="literal" x="35" y="335" width="85" height="25" rx="7"/><text class="text" x="45" y="352">MINVFUNC</text><rect class="literal" x="130" y="335" width="30" height="25" rx="7"/><text class="text" x="140" y="352">=</text><a xlink:href="../../../syntax_resources/grammar_diagrams#minvfunc"><rect class="rule" x="170" y="335" width="76" height="25"/><text class="text" x="180" y="352">minvfunc</text></a><rect class="literal" x="35" y="365" width="67" height="25" rx="7"/><text class="text" x="45" y="382">MSTYPE</text><rect class="literal" x="112" y="365" width="30" height="25" rx="7"/><text class="text" x="122" y="382">=</text><a xlink:href="../../../syntax_resources/grammar_diagrams#mstate-data-type"><rect class="rule" x="152" y="365" width="130" height="25"/><text class="text" x="162" y="382">mstate_data_type</text></a><rect class="literal" x="35" y="395" width="77" height="25" rx="7"/><text class="text" x="45" y="412">MSSPACE</text><rect class="literal" x="122" y="395" width="30" height="25" rx="7"/><text class="text" x="132" y="412">=</text><a xlink:href="../../../syntax_resources/grammar_diagrams#mstate-data-size"><rect class="rule" x="162" y="395" width="126" height="25"/><text class="text" x="172" y="412">mstate_data_size</text></a><rect class="literal" x="35" y="425" width="99" height="25" rx="7"/><text class="text" x="45" y="442">MFINALFUNC</text><rect class="literal" x="144" y="425" width="30" height="25" rx="7"/><text class="text" x="154" y="442">=</text><a xlink:href="../../../syntax_resources/grammar_diagrams#mffunc"><rect class="rule" x="184" y="425" width="62" height="25"/><text class="text" x="194" y="442">mffunc</text></a><rect class="literal" x="35" y="455" width="146" height="25" rx="7"/><text class="text" x="45" y="472">MFINALFUNC_EXTRA</text><rect class="literal" x="35" y="485" width="154" height="25" rx="7"/><text class="text" x="45" y="502">MFINALFUNC_MODIFY</text><rect class="literal" x="199" y="485" width="30" height="25" rx="7"/><text class="text" x="209" y="502">=</text><rect class="literal" x="259" y="485" width="93" height="25" rx="7"/><text class="text" x="269" y="502">READ_ONLY</text><rect class="literal" x="259" y="515" width="92" height="25" rx="7"/><text class="text" x="269" y="532">SHAREABLE</text><rect class="literal" x="259" y="545" width="101" height="25" rx="7"/><text class="text" x="269" y="562">READ_WRITE</text><rect class="literal" x="35" y="575" width="91" height="25" rx="7"/><text class="text" x="45" y="592">MINITCOND</text><rect class="literal" x="136" y="575" width="30" height="25" rx="7"/><text class="text" x="146" y="592">=</text><a xlink:href="../../../syntax_resources/grammar_diagrams#minitial-condition"><rect class="rule" x="176" y="575" width="126" height="25"/><text class="text" x="186" y="592">minitial_condition</text></a><rect class="literal" x="35" y="605" width="68" height="25" rx="7"/><text class="text" x="45" y="622">SORTOP</text><rect class="literal" x="113" y="605" width="30" height="25" rx="7"/><text class="text" x="123" y="622">=</text><a xlink:href="../../../syntax_resources/grammar_diagrams#sort-operator"><rect class="rule" x="153" y="605" width="101" height="25"/><text class="text" x="163" y="622">sort_operator</text></a><rect class="literal" x="35" y="635" width="80" height="25" rx="7"/><text class="text" x="45" y="652">PARALLEL</text><rect class="literal" x="125" y="635" width="30" height="25" rx="7"/><text class="text" x="135" y="652">=</text><rect class="literal" x="185" y="635" width="51" height="25" rx="7"/><text class="text" x="195" y="652">SAFE</text><rect class="literal" x="185" y="665" width="96" height="25" rx="7"/><text class="text" x="195" y="682">RESTRICTED</text><rect class="literal" x="185" y="695" width="69" height="25" rx="7"/><text class="text" x="195" y="712">UNSAFE</text><polygon points="411,29 415,29 415,15 411,15" style="fill:black;stroke-width:0"/></svg>

#### aggregate_order_by_option

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="405" height="335" viewbox="0 0 405 335"><path class="connector" d="M0 22h35m67 0h10m30 0h10m114 0h124m-365 25q0 5 5 5h5m89 0h10m30 0h10m50 0h151q5 0 5-5m-355 30q0 5 5 5h5m136 0h204q5 0 5-5m-355 30q0 5 5 5h5m144 0h10m30 0h30m93 0h28m-131 25q0 5 5 5h5m92 0h14q5 0 5-5m-126-25q5 0 5 5v50q0 5 5 5h5m101 0h5q5 0 5-5v-50q0-5 5-5m5 0h5q5 0 5-5m-355 90q0 5 5 5h5m81 0h10m30 0h10m114 0h95q5 0 5-5m-355 30q0 5 5 5h5m80 0h10m30 0h30m51 0h65m-126 25q0 5 5 5h5m96 0h5q5 0 5-5m-121-25q5 0 5 5v50q0 5 5 5h5m69 0h32q5 0 5-5v-50q0-5 5-5m5 0h74q5 0 5-5m-360-205q5 0 5 5v290q0 5 5 5h5m111 0h229q5 0 5-5v-290q0-5 5-5m5 0h15"/><polygon points="0,29 5,22 0,15" style="fill:black;stroke-width:0"/><rect class="literal" x="35" y="5" width="67" height="25" rx="7"/><text class="text" x="45" y="22">SSPACE</text><rect class="literal" x="112" y="5" width="30" height="25" rx="7"/><text class="text" x="122" y="22">=</text><a xlink:href="../../../syntax_resources/grammar_diagrams#state-data-size"><rect class="rule" x="152" y="5" width="114" height="25"/><text class="text" x="162" y="22">state_data_size</text></a><rect class="literal" x="35" y="35" width="89" height="25" rx="7"/><text class="text" x="45" y="52">FINALFUNC</text><rect class="literal" x="134" y="35" width="30" height="25" rx="7"/><text class="text" x="144" y="52">=</text><a xlink:href="../../../syntax_resources/grammar_diagrams#ffunc"><rect class="rule" x="174" y="35" width="50" height="25"/><text class="text" x="184" y="52">ffunc</text></a><rect class="literal" x="35" y="65" width="136" height="25" rx="7"/><text class="text" x="45" y="82">FINALFUNC_EXTRA</text><rect class="literal" x="35" y="95" width="144" height="25" rx="7"/><text class="text" x="45" y="112">FINALFUNC_MODIFY</text><rect class="literal" x="189" y="95" width="30" height="25" rx="7"/><text class="text" x="199" y="112">=</text><rect class="literal" x="249" y="95" width="93" height="25" rx="7"/><text class="text" x="259" y="112">READ_ONLY</text><rect class="literal" x="249" y="125" width="92" height="25" rx="7"/><text class="text" x="259" y="142">SHAREABLE</text><rect class="literal" x="249" y="155" width="101" height="25" rx="7"/><text class="text" x="259" y="172">READ_WRITE</text><rect class="literal" x="35" y="185" width="81" height="25" rx="7"/><text class="text" x="45" y="202">INITCOND</text><rect class="literal" x="126" y="185" width="30" height="25" rx="7"/><text class="text" x="136" y="202">=</text><a xlink:href="../../../syntax_resources/grammar_diagrams#initial-condition"><rect class="rule" x="166" y="185" width="114" height="25"/><text class="text" x="176" y="202">initial_condition</text></a><rect class="literal" x="35" y="215" width="80" height="25" rx="7"/><text class="text" x="45" y="232">PARALLEL</text><rect class="literal" x="125" y="215" width="30" height="25" rx="7"/><text class="text" x="135" y="232">=</text><rect class="literal" x="185" y="215" width="51" height="25" rx="7"/><text class="text" x="195" y="232">SAFE</text><rect class="literal" x="185" y="245" width="96" height="25" rx="7"/><text class="text" x="195" y="262">RESTRICTED</text><rect class="literal" x="185" y="275" width="69" height="25" rx="7"/><text class="text" x="195" y="292">UNSAFE</text><rect class="literal" x="35" y="305" width="111" height="25" rx="7"/><text class="text" x="45" y="322">HYPOTHETICAL</text><polygon points="401,29 405,29 405,15 401,15" style="fill:black;stroke-width:0"/></svg>

#### aggregate_old_option

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="415" height="635" viewbox="0 0 415 635"><path class="connector" d="M0 22h35m67 0h10m30 0h10m114 0h134m-375 25q0 5 5 5h5m89 0h10m30 0h10m50 0h161q5 0 5-5m-365 30q0 5 5 5h5m136 0h214q5 0 5-5m-365 30q0 5 5 5h5m144 0h10m30 0h30m93 0h28m-131 25q0 5 5 5h5m92 0h14q5 0 5-5m-126-25q5 0 5 5v50q0 5 5 5h5m101 0h5q5 0 5-5v-50q0-5 5-5m5 0h15q5 0 5-5m-365 90q0 5 5 5h5m110 0h10m30 0h10m96 0h94q5 0 5-5m-365 30q0 5 5 5h5m97 0h10m30 0h10m77 0h126q5 0 5-5m-365 30q0 5 5 5h5m114 0h10m30 0h10m91 0h95q5 0 5-5m-365 30q0 5 5 5h5m81 0h10m30 0h10m114 0h105q5 0 5-5m-365 30q0 5 5 5h5m71 0h10m30 0h10m64 0h165q5 0 5-5m-365 30q0 5 5 5h5m85 0h10m30 0h10m76 0h139q5 0 5-5m-365 30q0 5 5 5h5m67 0h10m30 0h10m130 0h103q5 0 5-5m-365 30q0 5 5 5h5m77 0h10m30 0h10m126 0h97q5 0 5-5m-365 30q0 5 5 5h5m99 0h10m30 0h10m62 0h139q5 0 5-5m-365 30q0 5 5 5h5m146 0h204q5 0 5-5m-365 30q0 5 5 5h5m154 0h10m30 0h30m93 0h28m-131 25q0 5 5 5h5m92 0h14q5 0 5-5m-126-25q5 0 5 5v50q0 5 5 5h5m101 0h5q5 0 5-5v-50q0-5 5-5m5 0h5q5 0 5-5m-365 90q0 5 5 5h5m91 0h10m30 0h10m126 0h83q5 0 5-5m-370-565q5 0 5 5v590q0 5 5 5h5m68 0h10m30 0h10m101 0h131q5 0 5-5v-590q0-5 5-5m5 0h15"/><polygon points="0,29 5,22 0,15" style="fill:black;stroke-width:0"/><rect class="literal" x="35" y="5" width="67" height="25" rx="7"/><text class="text" x="45" y="22">SSPACE</text><rect class="literal" x="112" y="5" width="30" height="25" rx="7"/><text class="text" x="122" y="22">=</text><a xlink:href="../../../syntax_resources/grammar_diagrams#state-data-size"><rect class="rule" x="152" y="5" width="114" height="25"/><text class="text" x="162" y="22">state_data_size</text></a><rect class="literal" x="35" y="35" width="89" height="25" rx="7"/><text class="text" x="45" y="52">FINALFUNC</text><rect class="literal" x="134" y="35" width="30" height="25" rx="7"/><text class="text" x="144" y="52">=</text><a xlink:href="../../../syntax_resources/grammar_diagrams#ffunc"><rect class="rule" x="174" y="35" width="50" height="25"/><text class="text" x="184" y="52">ffunc</text></a><rect class="literal" x="35" y="65" width="136" height="25" rx="7"/><text class="text" x="45" y="82">FINALFUNC_EXTRA</text><rect class="literal" x="35" y="95" width="144" height="25" rx="7"/><text class="text" x="45" y="112">FINALFUNC_MODIFY</text><rect class="literal" x="189" y="95" width="30" height="25" rx="7"/><text class="text" x="199" y="112">=</text><rect class="literal" x="249" y="95" width="93" height="25" rx="7"/><text class="text" x="259" y="112">READ_ONLY</text><rect class="literal" x="249" y="125" width="92" height="25" rx="7"/><text class="text" x="259" y="142">SHAREABLE</text><rect class="literal" x="249" y="155" width="101" height="25" rx="7"/><text class="text" x="259" y="172">READ_WRITE</text><rect class="literal" x="35" y="185" width="110" height="25" rx="7"/><text class="text" x="45" y="202">COMBINEFUNC</text><rect class="literal" x="155" y="185" width="30" height="25" rx="7"/><text class="text" x="165" y="202">=</text><a xlink:href="../../../syntax_resources/grammar_diagrams#combinefunc"><rect class="rule" x="195" y="185" width="96" height="25"/><text class="text" x="205" y="202">combinefunc</text></a><rect class="literal" x="35" y="215" width="97" height="25" rx="7"/><text class="text" x="45" y="232">SERIALFUNC</text><rect class="literal" x="142" y="215" width="30" height="25" rx="7"/><text class="text" x="152" y="232">=</text><a xlink:href="../../../syntax_resources/grammar_diagrams#serialfunc"><rect class="rule" x="182" y="215" width="77" height="25"/><text class="text" x="192" y="232">serialfunc</text></a><rect class="literal" x="35" y="245" width="114" height="25" rx="7"/><text class="text" x="45" y="262">DESERIALFUNC</text><rect class="literal" x="159" y="245" width="30" height="25" rx="7"/><text class="text" x="169" y="262">=</text><a xlink:href="../../../syntax_resources/grammar_diagrams#deserialfunc"><rect class="rule" x="199" y="245" width="91" height="25"/><text class="text" x="209" y="262">deserialfunc</text></a><rect class="literal" x="35" y="275" width="81" height="25" rx="7"/><text class="text" x="45" y="292">INITCOND</text><rect class="literal" x="126" y="275" width="30" height="25" rx="7"/><text class="text" x="136" y="292">=</text><a xlink:href="../../../syntax_resources/grammar_diagrams#initial-condition"><rect class="rule" x="166" y="275" width="114" height="25"/><text class="text" x="176" y="292">initial_condition</text></a><rect class="literal" x="35" y="305" width="71" height="25" rx="7"/><text class="text" x="45" y="322">MSFUNC</text><rect class="literal" x="116" y="305" width="30" height="25" rx="7"/><text class="text" x="126" y="322">=</text><a xlink:href="../../../syntax_resources/grammar_diagrams#msfunc"><rect class="rule" x="156" y="305" width="64" height="25"/><text class="text" x="166" y="322">msfunc</text></a><rect class="literal" x="35" y="335" width="85" height="25" rx="7"/><text class="text" x="45" y="352">MINVFUNC</text><rect class="literal" x="130" y="335" width="30" height="25" rx="7"/><text class="text" x="140" y="352">=</text><a xlink:href="../../../syntax_resources/grammar_diagrams#minvfunc"><rect class="rule" x="170" y="335" width="76" height="25"/><text class="text" x="180" y="352">minvfunc</text></a><rect class="literal" x="35" y="365" width="67" height="25" rx="7"/><text class="text" x="45" y="382">MSTYPE</text><rect class="literal" x="112" y="365" width="30" height="25" rx="7"/><text class="text" x="122" y="382">=</text><a xlink:href="../../../syntax_resources/grammar_diagrams#mstate-data-type"><rect class="rule" x="152" y="365" width="130" height="25"/><text class="text" x="162" y="382">mstate_data_type</text></a><rect class="literal" x="35" y="395" width="77" height="25" rx="7"/><text class="text" x="45" y="412">MSSPACE</text><rect class="literal" x="122" y="395" width="30" height="25" rx="7"/><text class="text" x="132" y="412">=</text><a xlink:href="../../../syntax_resources/grammar_diagrams#mstate-data-size"><rect class="rule" x="162" y="395" width="126" height="25"/><text class="text" x="172" y="412">mstate_data_size</text></a><rect class="literal" x="35" y="425" width="99" height="25" rx="7"/><text class="text" x="45" y="442">MFINALFUNC</text><rect class="literal" x="144" y="425" width="30" height="25" rx="7"/><text class="text" x="154" y="442">=</text><a xlink:href="../../../syntax_resources/grammar_diagrams#mffunc"><rect class="rule" x="184" y="425" width="62" height="25"/><text class="text" x="194" y="442">mffunc</text></a><rect class="literal" x="35" y="455" width="146" height="25" rx="7"/><text class="text" x="45" y="472">MFINALFUNC_EXTRA</text><rect class="literal" x="35" y="485" width="154" height="25" rx="7"/><text class="text" x="45" y="502">MFINALFUNC_MODIFY</text><rect class="literal" x="199" y="485" width="30" height="25" rx="7"/><text class="text" x="209" y="502">=</text><rect class="literal" x="259" y="485" width="93" height="25" rx="7"/><text class="text" x="269" y="502">READ_ONLY</text><rect class="literal" x="259" y="515" width="92" height="25" rx="7"/><text class="text" x="269" y="532">SHAREABLE</text><rect class="literal" x="259" y="545" width="101" height="25" rx="7"/><text class="text" x="269" y="562">READ_WRITE</text><rect class="literal" x="35" y="575" width="91" height="25" rx="7"/><text class="text" x="45" y="592">MINITCOND</text><rect class="literal" x="136" y="575" width="30" height="25" rx="7"/><text class="text" x="146" y="592">=</text><a xlink:href="../../../syntax_resources/grammar_diagrams#minitial-condition"><rect class="rule" x="176" y="575" width="126" height="25"/><text class="text" x="186" y="592">minitial_condition</text></a><rect class="literal" x="35" y="605" width="68" height="25" rx="7"/><text class="text" x="45" y="622">SORTOP</text><rect class="literal" x="113" y="605" width="30" height="25" rx="7"/><text class="text" x="123" y="622">=</text><a xlink:href="../../../syntax_resources/grammar_diagrams#sort-operator"><rect class="rule" x="153" y="605" width="101" height="25"/><text class="text" x="163" y="622">sort_operator</text></a><polygon points="411,29 415,29 415,15 411,15" style="fill:black;stroke-width:0"/></svg>
