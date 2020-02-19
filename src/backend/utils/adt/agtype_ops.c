/*
 * Copyright 2020 Bitnine Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Functions for operators in Cypher expressions.
 */

#include "postgres.h"

#include <math.h>

#include "utils/builtins.h"
#include "utils/numeric.h"

#include "utils/agtype.h"

PG_FUNCTION_INFO_V1(agtype_add);

/*
 * agtype addition function for + operator
 */
Datum agtype_add(PG_FUNCTION_ARGS)
{
    agtype *lhs = AG_GET_ARG_AGTYPE_P(0);
    agtype *rhs = AG_GET_ARG_AGTYPE_P(1);
    agtype_value *agtv_lhs;
    agtype_value *agtv_rhs;
    agtype_value agtv_result;
    size_t len;
    char *buffer;
    Datum n;
    char *nstr;
    char *fstr;
    size_t nlen;
    size_t flen;

    if (!(AGT_ROOT_IS_SCALAR(lhs)) || !(AGT_ROOT_IS_SCALAR(rhs)))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("must be scalar value, not array or object")));

        PG_RETURN_NULL();
    }

    agtv_lhs = get_ith_agtype_value_from_container(&lhs->root, 0);
    agtv_rhs = get_ith_agtype_value_from_container(&rhs->root, 0);

    if (agtv_lhs->type == AGTV_STRING && agtv_rhs->type == AGTV_STRING)
    {
        len = agtv_lhs->val.string.len + agtv_rhs->val.string.len;
        check_string_length(len);
        buffer = palloc(len);

        strncpy(buffer, agtv_lhs->val.string.val, agtv_lhs->val.string.len);
        strncpy(buffer + agtv_lhs->val.string.len, agtv_rhs->val.string.val,
                agtv_rhs->val.string.len);

        agtv_result.type = AGTV_STRING;
        agtv_result.val.string.len = len;
        agtv_result.val.string.val = buffer;

        AG_RETURN_AGTYPE_P(agtype_value_to_agtype(&agtv_result));
    }
    if (agtv_lhs->type == AGTV_STRING)
    {
        if (agtv_rhs->type == AGTV_INTEGER || agtv_rhs->type == AGTV_FLOAT)
        {
            if (agtv_rhs->type == AGTV_INTEGER)
            {
                n = DirectFunctionCall1(int8out,
                                        Int8GetDatum(agtv_rhs->val.int_value));
                nstr = DatumGetCString(n);
                nlen = strlen(nstr);
            }
            else
            {
                n = DirectFunctionCall1(
                    float8out, Float8GetDatum(agtv_rhs->val.float_value));
                fstr = DatumGetCString(n);

                if(is_decimal_needed(fstr))
                {
                    flen = strlen(fstr);
                    nlen = flen + strlen(".0");
                    nstr = palloc(nlen);
                    strncpy(nstr, fstr, strlen(fstr));
                    strncpy(nstr + flen, ".0", strlen(".0"));
                }
                else
                {
                    nstr = DatumGetCString(n);
                    nlen = strlen(nstr);
                }
            }

            len = agtv_lhs->val.string.len + nlen;
            check_string_length(len);
            buffer = palloc(len);

            strncpy(buffer, agtv_lhs->val.string.val,
                    agtv_lhs->val.string.len);
            strncpy(buffer + agtv_lhs->val.string.len, nstr, nlen);

            agtv_result.type = AGTV_STRING;
            agtv_result.val.string.len = len;
            agtv_result.val.string.val = buffer;

            AG_RETURN_AGTYPE_P(agtype_value_to_agtype(&agtv_result));
        }
    }
    if (agtv_rhs->type == AGTV_STRING)
    {
        if (agtv_lhs->type == AGTV_INTEGER || agtv_lhs->type == AGTV_FLOAT)
        {
            if (agtv_lhs->type == AGTV_INTEGER)
            {
                n = DirectFunctionCall1(int8out,
                                        Int8GetDatum(agtv_lhs->val.int_value));
                nstr = DatumGetCString(n);
                nlen = strlen(nstr);
            }
            else
            {
                n = DirectFunctionCall1(
                    float8out, Float8GetDatum(agtv_lhs->val.float_value));
                fstr = DatumGetCString(n);

                if(is_decimal_needed(fstr))
                {
                    flen = strlen(fstr);
                    nlen = flen + strlen(".0");
                    nstr = palloc(nlen);
                    strncpy(nstr, fstr, strlen(fstr));
                    strncpy(nstr + flen, ".0", strlen(".0"));
                }
                else
                {
                    nstr = DatumGetCString(n);
                    nlen = strlen(nstr);
                }
            }

            len = agtv_rhs->val.string.len + nlen;
            check_string_length(len);
            buffer = palloc(len);

            strncpy(buffer, nstr, nlen);
            strncpy(buffer + nlen, agtv_rhs->val.string.val,
                    agtv_rhs->val.string.len);

            agtv_result.type = AGTV_STRING;
            agtv_result.val.string.len = len;
            agtv_result.val.string.val = buffer;

            AG_RETURN_AGTYPE_P(agtype_value_to_agtype(&agtv_result));
        }
    }
    if (agtv_lhs->type == AGTV_INTEGER && agtv_rhs->type == AGTV_INTEGER)
    {
        agtv_result.type = AGTV_INTEGER;
        agtv_result.val.int_value = agtv_lhs->val.int_value +
                                    agtv_rhs->val.int_value;
        AG_RETURN_AGTYPE_P(agtype_value_to_agtype(&agtv_result));
    }
    if (agtv_lhs->type == AGTV_FLOAT && agtv_rhs->type == AGTV_FLOAT)
    {
        agtv_result.type = AGTV_FLOAT;
        agtv_result.val.float_value = agtv_lhs->val.float_value +
                                      agtv_rhs->val.float_value;
        AG_RETURN_AGTYPE_P(agtype_value_to_agtype(&agtv_result));
    }
    if (agtv_lhs->type == AGTV_FLOAT && agtv_rhs->type == AGTV_INTEGER)
    {
        agtv_result.type = AGTV_FLOAT;
        agtv_result.val.float_value = agtv_lhs->val.float_value +
                                      agtv_rhs->val.int_value;
        AG_RETURN_AGTYPE_P(agtype_value_to_agtype(&agtv_result));
    }
    if (agtv_lhs->type == AGTV_INTEGER && agtv_rhs->type == AGTV_FLOAT)
    {
        agtv_result.type = AGTV_FLOAT;
        agtv_result.val.float_value = agtv_lhs->val.int_value +
                                      agtv_rhs->val.float_value;
        AG_RETURN_AGTYPE_P(agtype_value_to_agtype(&agtv_result));
    }

    ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                    errmsg("Invalid input parameter types for agtype_add")));

    PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(agtype_sub);

/*
 * agtype subtraction function for - operator
 */
Datum agtype_sub(PG_FUNCTION_ARGS)
{
    agtype *lhs = AG_GET_ARG_AGTYPE_P(0);
    agtype *rhs = AG_GET_ARG_AGTYPE_P(1);
    agtype_value *agtv_lhs;
    agtype_value *agtv_rhs;
    agtype_value agtv_result;

    if (!(AGT_ROOT_IS_SCALAR(lhs)) || !(AGT_ROOT_IS_SCALAR(rhs)))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("must be scalar value, not array or object")));

        PG_RETURN_NULL();
    }

    agtv_lhs = get_ith_agtype_value_from_container(&lhs->root, 0);
    agtv_rhs = get_ith_agtype_value_from_container(&rhs->root, 0);

    if (agtv_lhs->type == AGTV_INTEGER && agtv_rhs->type == AGTV_INTEGER)
    {
        agtv_result.type = AGTV_INTEGER;
        agtv_result.val.int_value = agtv_lhs->val.int_value -
                                    agtv_rhs->val.int_value;
        AG_RETURN_AGTYPE_P(agtype_value_to_agtype(&agtv_result));
    }
    if (agtv_lhs->type == AGTV_FLOAT && agtv_rhs->type == AGTV_FLOAT)
    {
        agtv_result.type = AGTV_FLOAT;
        agtv_result.val.float_value = agtv_lhs->val.float_value -
                                      agtv_rhs->val.float_value;
        AG_RETURN_AGTYPE_P(agtype_value_to_agtype(&agtv_result));
    }
    if (agtv_lhs->type == AGTV_FLOAT && agtv_rhs->type == AGTV_INTEGER)
    {
        agtv_result.type = AGTV_FLOAT;
        agtv_result.val.float_value = agtv_lhs->val.float_value -
                                      agtv_rhs->val.int_value;
        AG_RETURN_AGTYPE_P(agtype_value_to_agtype(&agtv_result));
    }
    if (agtv_lhs->type == AGTV_INTEGER && agtv_rhs->type == AGTV_FLOAT)
    {
        agtv_result.type = AGTV_FLOAT;
        agtv_result.val.float_value = agtv_lhs->val.int_value -
                                      agtv_rhs->val.float_value;
        AG_RETURN_AGTYPE_P(agtype_value_to_agtype(&agtv_result));
    }

    ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                    errmsg("Invalid input parameter types for agtype_sub")));

    PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(agtype_neg);

/*
 * agtype negation function for unary - operator
 */
Datum agtype_neg(PG_FUNCTION_ARGS)
{
    agtype *v = AG_GET_ARG_AGTYPE_P(0);
    agtype_value *agtv_value;
    agtype_value agtv_result;

    if (!(AGT_ROOT_IS_SCALAR(v)))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("must be scalar value, not array or object")));

        PG_RETURN_NULL();
    }

    agtv_value = get_ith_agtype_value_from_container(&v->root, 0);

    if (agtv_value->type == AGTV_INTEGER)
    {
        agtv_result.type = AGTV_INTEGER;
        agtv_result.val.int_value = -agtv_value->val.int_value;
        AG_RETURN_AGTYPE_P(agtype_value_to_agtype(&agtv_result));
    }
    if (agtv_value->type == AGTV_FLOAT)
    {
        agtv_result.type = AGTV_FLOAT;
        agtv_result.val.float_value = -agtv_value->val.float_value;
        AG_RETURN_AGTYPE_P(agtype_value_to_agtype(&agtv_result));
    }

    ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                    errmsg("Invalid input parameter type for agtype_neg")));

    PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(agtype_mul);

/*
 * agtype multiplication function for * operator
 */
Datum agtype_mul(PG_FUNCTION_ARGS)
{
    agtype *lhs = AG_GET_ARG_AGTYPE_P(0);
    agtype *rhs = AG_GET_ARG_AGTYPE_P(1);
    agtype_value *agtv_lhs;
    agtype_value *agtv_rhs;
    agtype_value agtv_result;

    if (!(AGT_ROOT_IS_SCALAR(lhs)) || !(AGT_ROOT_IS_SCALAR(rhs)))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("must be scalar value, not array or object")));

        PG_RETURN_NULL();
    }

    agtv_lhs = get_ith_agtype_value_from_container(&lhs->root, 0);
    agtv_rhs = get_ith_agtype_value_from_container(&rhs->root, 0);

    if (agtv_lhs->type == AGTV_INTEGER && agtv_rhs->type == AGTV_INTEGER)
    {
        agtv_result.type = AGTV_INTEGER;
        agtv_result.val.int_value = agtv_lhs->val.int_value *
                                    agtv_rhs->val.int_value;
        AG_RETURN_AGTYPE_P(agtype_value_to_agtype(&agtv_result));
    }
    if (agtv_lhs->type == AGTV_FLOAT && agtv_rhs->type == AGTV_FLOAT)
    {
        agtv_result.type = AGTV_FLOAT;
        agtv_result.val.float_value = agtv_lhs->val.float_value *
                                      agtv_rhs->val.float_value;
        AG_RETURN_AGTYPE_P(agtype_value_to_agtype(&agtv_result));
    }
    if (agtv_lhs->type == AGTV_FLOAT && agtv_rhs->type == AGTV_INTEGER)
    {
        agtv_result.type = AGTV_FLOAT;
        agtv_result.val.float_value = agtv_lhs->val.float_value *
                                      agtv_rhs->val.int_value;
        AG_RETURN_AGTYPE_P(agtype_value_to_agtype(&agtv_result));
    }
    if (agtv_lhs->type == AGTV_INTEGER && agtv_rhs->type == AGTV_FLOAT)
    {
        agtv_result.type = AGTV_FLOAT;
        agtv_result.val.float_value = agtv_lhs->val.int_value *
                                      agtv_rhs->val.float_value;
        AG_RETURN_AGTYPE_P(agtype_value_to_agtype(&agtv_result));
    }

    ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                    errmsg("Invalid input parameter types for agtype_mul")));

    PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(agtype_div);

/*
 * agtype division function for / operator
 */
Datum agtype_div(PG_FUNCTION_ARGS)
{
    agtype *lhs = AG_GET_ARG_AGTYPE_P(0);
    agtype *rhs = AG_GET_ARG_AGTYPE_P(1);
    agtype_value *agtv_lhs;
    agtype_value *agtv_rhs;
    agtype_value agtv_result;

    if (!(AGT_ROOT_IS_SCALAR(lhs)) || !(AGT_ROOT_IS_SCALAR(rhs)))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("must be scalar value, not array or object")));

        PG_RETURN_NULL();
    }

    agtv_lhs = get_ith_agtype_value_from_container(&lhs->root, 0);
    agtv_rhs = get_ith_agtype_value_from_container(&rhs->root, 0);

    if (agtv_lhs->type == AGTV_INTEGER && agtv_rhs->type == AGTV_INTEGER)
    {
        if (agtv_rhs->val.int_value == 0)
        {
            ereport(ERROR, (errcode(ERRCODE_DIVISION_BY_ZERO),
                            errmsg("division by zero")));
            PG_RETURN_NULL();
        }

        agtv_result.type = AGTV_INTEGER;
        agtv_result.val.int_value = agtv_lhs->val.int_value /
                                    agtv_rhs->val.int_value;
        AG_RETURN_AGTYPE_P(agtype_value_to_agtype(&agtv_result));
    }
    if (agtv_lhs->type == AGTV_FLOAT && agtv_rhs->type == AGTV_FLOAT)
    {
        if (agtv_rhs->val.float_value == 0)
        {
            ereport(ERROR, (errcode(ERRCODE_DIVISION_BY_ZERO),
                            errmsg("division by zero")));
            PG_RETURN_NULL();
        }

        agtv_result.type = AGTV_FLOAT;
        agtv_result.val.float_value = agtv_lhs->val.float_value /
                                      agtv_rhs->val.float_value;
        AG_RETURN_AGTYPE_P(agtype_value_to_agtype(&agtv_result));
    }
    if (agtv_lhs->type == AGTV_FLOAT && agtv_rhs->type == AGTV_INTEGER)
    {
        if (agtv_rhs->val.int_value == 0)
        {
            ereport(ERROR, (errcode(ERRCODE_DIVISION_BY_ZERO),
                            errmsg("division by zero")));
            PG_RETURN_NULL();
        }

        agtv_result.type = AGTV_FLOAT;
        agtv_result.val.float_value = agtv_lhs->val.float_value /
                                      agtv_rhs->val.int_value;
        AG_RETURN_AGTYPE_P(agtype_value_to_agtype(&agtv_result));
    }
    if (agtv_lhs->type == AGTV_INTEGER && agtv_rhs->type == AGTV_FLOAT)
    {
        if (agtv_rhs->val.float_value == 0)
        {
            ereport(ERROR, (errcode(ERRCODE_DIVISION_BY_ZERO),
                            errmsg("division by zero")));
            PG_RETURN_NULL();
        }

        agtv_result.type = AGTV_FLOAT;
        agtv_result.val.float_value = agtv_lhs->val.int_value /
                                      agtv_rhs->val.float_value;
        AG_RETURN_AGTYPE_P(agtype_value_to_agtype(&agtv_result));
    }

    ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                    errmsg("Invalid input parameter types for agtype_div")));

    PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(agtype_mod);

/*
 * agtype modulo function for % operator
 */
Datum agtype_mod(PG_FUNCTION_ARGS)
{
    agtype *lhs = AG_GET_ARG_AGTYPE_P(0);
    agtype *rhs = AG_GET_ARG_AGTYPE_P(1);
    agtype_value *agtv_lhs;
    agtype_value *agtv_rhs;
    agtype_value agtv_result;

    if (!(AGT_ROOT_IS_SCALAR(lhs)) || !(AGT_ROOT_IS_SCALAR(rhs)))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("must be scalar value, not array or object")));

        PG_RETURN_NULL();
    }

    agtv_lhs = get_ith_agtype_value_from_container(&lhs->root, 0);
    agtv_rhs = get_ith_agtype_value_from_container(&rhs->root, 0);

    if (agtv_lhs->type == AGTV_INTEGER && agtv_rhs->type == AGTV_INTEGER)
    {
        agtv_result.type = AGTV_INTEGER;
        agtv_result.val.int_value = agtv_lhs->val.int_value %
                                    agtv_rhs->val.int_value;
        AG_RETURN_AGTYPE_P(agtype_value_to_agtype(&agtv_result));
    }
    if (agtv_lhs->type == AGTV_FLOAT && agtv_rhs->type == AGTV_FLOAT)
    {
        agtv_result.type = AGTV_FLOAT;
        agtv_result.val.float_value = fmod(agtv_lhs->val.float_value,
                                           agtv_rhs->val.float_value);
        AG_RETURN_AGTYPE_P(agtype_value_to_agtype(&agtv_result));
    }
    if (agtv_lhs->type == AGTV_FLOAT && agtv_rhs->type == AGTV_INTEGER)
    {
        agtv_result.type = AGTV_FLOAT;
        agtv_result.val.float_value = fmod(agtv_lhs->val.float_value,
                                           agtv_rhs->val.int_value);
        AG_RETURN_AGTYPE_P(agtype_value_to_agtype(&agtv_result));
    }
    if (agtv_lhs->type == AGTV_INTEGER && agtv_rhs->type == AGTV_FLOAT)
    {
        agtv_result.type = AGTV_FLOAT;
        agtv_result.val.float_value = fmod(agtv_lhs->val.int_value,
                                           agtv_rhs->val.float_value);
        AG_RETURN_AGTYPE_P(agtype_value_to_agtype(&agtv_result));
    }

    ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                    errmsg("Invalid input parameter types for agtype_mod")));

    PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(agtype_pow);

/*
 * agtype power function for ^ operator
 */
Datum agtype_pow(PG_FUNCTION_ARGS)
{
    agtype *lhs = AG_GET_ARG_AGTYPE_P(0);
    agtype *rhs = AG_GET_ARG_AGTYPE_P(1);
    agtype_value *agtv_lhs;
    agtype_value *agtv_rhs;
    agtype_value agtv_result;

    if (!(AGT_ROOT_IS_SCALAR(lhs)) || !(AGT_ROOT_IS_SCALAR(rhs)))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("must be scalar value, not array or object")));

        PG_RETURN_NULL();
    }

    agtv_lhs = get_ith_agtype_value_from_container(&lhs->root, 0);
    agtv_rhs = get_ith_agtype_value_from_container(&rhs->root, 0);

    if (agtv_lhs->type == AGTV_INTEGER && agtv_rhs->type == AGTV_INTEGER)
    {
        agtv_result.type = AGTV_FLOAT;
        agtv_result.val.float_value = pow(agtv_lhs->val.int_value,
                                          agtv_rhs->val.int_value);
        AG_RETURN_AGTYPE_P(agtype_value_to_agtype(&agtv_result));
    }
    if (agtv_lhs->type == AGTV_FLOAT && agtv_rhs->type == AGTV_FLOAT)
    {
        agtv_result.type = AGTV_FLOAT;
        agtv_result.val.float_value = pow(agtv_lhs->val.float_value,
                                          agtv_rhs->val.float_value);
        AG_RETURN_AGTYPE_P(agtype_value_to_agtype(&agtv_result));
    }
    if (agtv_lhs->type == AGTV_FLOAT && agtv_rhs->type == AGTV_INTEGER)
    {
        agtv_result.type = AGTV_FLOAT;
        agtv_result.val.float_value = pow(agtv_lhs->val.float_value,
                                          agtv_rhs->val.int_value);
        AG_RETURN_AGTYPE_P(agtype_value_to_agtype(&agtv_result));
    }
    if (agtv_lhs->type == AGTV_INTEGER && agtv_rhs->type == AGTV_FLOAT)
    {
        agtv_result.type = AGTV_FLOAT;
        agtv_result.val.float_value = pow(agtv_lhs->val.int_value,
                                          agtv_rhs->val.float_value);
        AG_RETURN_AGTYPE_P(agtype_value_to_agtype(&agtv_result));
    }

    ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                    errmsg("Invalid input parameter types for agtype_pow")));

    PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(agtype_eq);

Datum agtype_eq(PG_FUNCTION_ARGS)
{
    agtype *agtype_lhs = AG_GET_ARG_AGTYPE_P(0);
    agtype *agtype_rhs = AG_GET_ARG_AGTYPE_P(1);
    bool result;

    result = (compare_agtype_containers_orderability(&agtype_lhs->root,
                                                     &agtype_rhs->root) == 0);

    PG_FREE_IF_COPY(agtype_lhs, 0);
    PG_FREE_IF_COPY(agtype_rhs, 1);

    PG_RETURN_BOOL(result);
}

PG_FUNCTION_INFO_V1(agtype_ne);

Datum agtype_ne(PG_FUNCTION_ARGS)
{
    agtype *agtype_lhs = AG_GET_ARG_AGTYPE_P(0);
    agtype *agtype_rhs = AG_GET_ARG_AGTYPE_P(1);
    bool result = true;

    result = (compare_agtype_containers_orderability(&agtype_lhs->root,
                                                     &agtype_rhs->root) != 0);

    PG_FREE_IF_COPY(agtype_lhs, 0);
    PG_FREE_IF_COPY(agtype_rhs, 1);

    PG_RETURN_BOOL(result);
}

PG_FUNCTION_INFO_V1(agtype_lt);

Datum agtype_lt(PG_FUNCTION_ARGS)
{
    agtype *agtype_lhs = AG_GET_ARG_AGTYPE_P(0);
    agtype *agtype_rhs = AG_GET_ARG_AGTYPE_P(1);
    bool result;

    result = (compare_agtype_containers_orderability(&agtype_lhs->root,
                                                     &agtype_rhs->root) < 0);

    PG_FREE_IF_COPY(agtype_lhs, 0);
    PG_FREE_IF_COPY(agtype_rhs, 1);

    PG_RETURN_BOOL(result);
}

PG_FUNCTION_INFO_V1(agtype_gt);

Datum agtype_gt(PG_FUNCTION_ARGS)
{
    agtype *agtype_lhs = AG_GET_ARG_AGTYPE_P(0);
    agtype *agtype_rhs = AG_GET_ARG_AGTYPE_P(1);
    bool result;

    result = (compare_agtype_containers_orderability(&agtype_lhs->root,
                                                     &agtype_rhs->root) > 0);

    PG_FREE_IF_COPY(agtype_lhs, 0);
    PG_FREE_IF_COPY(agtype_rhs, 1);

    PG_RETURN_BOOL(result);
}

PG_FUNCTION_INFO_V1(agtype_le);

Datum agtype_le(PG_FUNCTION_ARGS)
{
    agtype *agtype_lhs = AG_GET_ARG_AGTYPE_P(0);
    agtype *agtype_rhs = AG_GET_ARG_AGTYPE_P(1);
    bool result;

    result = (compare_agtype_containers_orderability(&agtype_lhs->root,
                                                     &agtype_rhs->root) <= 0);

    PG_FREE_IF_COPY(agtype_lhs, 0);
    PG_FREE_IF_COPY(agtype_rhs, 1);

    PG_RETURN_BOOL(result);
}

PG_FUNCTION_INFO_V1(agtype_ge);

Datum agtype_ge(PG_FUNCTION_ARGS)
{
    agtype *agtype_lhs = AG_GET_ARG_AGTYPE_P(0);
    agtype *agtype_rhs = AG_GET_ARG_AGTYPE_P(1);
    bool result;

    result = (compare_agtype_containers_orderability(&agtype_lhs->root,
                                                     &agtype_rhs->root) >= 0);

    PG_FREE_IF_COPY(agtype_lhs, 0);
    PG_FREE_IF_COPY(agtype_rhs, 1);

    PG_RETURN_BOOL(result);
}
