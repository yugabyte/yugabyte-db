#ifndef _PLVSTR_
#define _PLVSTR_

Datum plvstr_rvrs(PG_FUNCTION_ARGS);
Datum plvstr_normalize(PG_FUNCTION_ARGS);
Datum plvstr_is_prefix_text(PG_FUNCTION_ARGS);
Datum plvstr_is_prefix_int(PG_FUNCTION_ARGS);
Datum plvstr_is_prefix_int64(PG_FUNCTION_ARGS);
Datum plvstr_lpart(PG_FUNCTION_ARGS);
Datum plvstr_rpart(PG_FUNCTION_ARGS);
Datum plvstr_lstrip(PG_FUNCTION_ARGS);
Datum plvstr_rstrip(PG_FUNCTION_ARGS);
Datum plvstr_left(PG_FUNCTION_ARGS);
Datum plvstr_right(PG_FUNCTION_ARGS);
Datum plvstr_substr2(PG_FUNCTION_ARGS);
Datum plvstr_substr3(PG_FUNCTION_ARGS);
Datum plvstr_instr2(PG_FUNCTION_ARGS);
Datum plvstr_instr3(PG_FUNCTION_ARGS);
Datum plvstr_instr4(PG_FUNCTION_ARGS);
Datum plvstr_betwn_i(PG_FUNCTION_ARGS);
Datum plvstr_betwn_c(PG_FUNCTION_ARGS);
Datum plvstr_swap(PG_FUNCTION_ARGS);

Datum plvchr_nth(PG_FUNCTION_ARGS);
Datum plvchr_first(PG_FUNCTION_ARGS);
Datum plvchr_last(PG_FUNCTION_ARGS);
Datum plvchr_is_kind_i(PG_FUNCTION_ARGS);
Datum plvchr_is_kind_a(PG_FUNCTION_ARGS);
Datum plvchr_char_name(PG_FUNCTION_ARGS);

Datum oracle_substr2(PG_FUNCTION_ARGS);
Datum oracle_substr3(PG_FUNCTION_ARGS);

Datum orafce_listagg1_transfn(PG_FUNCTION_ARGS);
Datum orafce_listagg2_transfn(PG_FUNCTION_ARGS);
Datum orafce_listagg_finalfn(PG_FUNCTION_ARGS);

#endif
