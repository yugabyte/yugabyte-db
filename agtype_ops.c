/*
 * agtype_ops.c
 *    Functions for operators in Cypher expressions.
 *
 * Copyright (c) 2017 by Bitnine Global, Inc.
 *
 * IDENTIFICATION
 *    agtype_ops.c
 */

#include "postgres.h"

#include "utils/numeric.h"

#include "agtype.h"

#include <math.h>

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

    if (!(AGT_ROOT_IS_SCALAR(lhs)) || !(AGT_ROOT_IS_SCALAR(rhs)))
    {
        elog(ERROR, "only scalar operations are supported");
    }

    agtv_lhs = get_ith_agtype_value_from_container(&lhs->root, 0);
    agtv_rhs = get_ith_agtype_value_from_container(&rhs->root, 0);
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

    elog(ERROR, "opertion is not yet supported");
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
        elog(ERROR, "only scalar operations are supported");
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

    elog(ERROR, "opertion is not yet supported");

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
        elog(ERROR, "only scalar operations are supported");
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

    elog(ERROR, "opertion is not yet supported");

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
        elog(ERROR, "only scalar operations are supported");
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

    elog(ERROR, "opertion is not yet supported");

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
        elog(ERROR, "only scalar operations are supported");
    }

    agtv_lhs = get_ith_agtype_value_from_container(&lhs->root, 0);
    agtv_rhs = get_ith_agtype_value_from_container(&rhs->root, 0);

    if (agtv_lhs->type == AGTV_INTEGER && agtv_rhs->type == AGTV_INTEGER)
    {
        if (agtv_rhs->val.int_value == 0)
            elog(ERROR, "division by zero");

        agtv_result.type = AGTV_INTEGER;
        agtv_result.val.int_value = agtv_lhs->val.int_value /
                                    agtv_rhs->val.int_value;
        AG_RETURN_AGTYPE_P(agtype_value_to_agtype(&agtv_result));
    }
    if (agtv_lhs->type == AGTV_FLOAT && agtv_rhs->type == AGTV_FLOAT)
    {
        if (agtv_rhs->val.float_value == 0)
            elog(ERROR, "division by zero");

        agtv_result.type = AGTV_FLOAT;
        agtv_result.val.float_value = agtv_lhs->val.float_value /
                                      agtv_rhs->val.float_value;
        AG_RETURN_AGTYPE_P(agtype_value_to_agtype(&agtv_result));
    }
    if (agtv_lhs->type == AGTV_FLOAT && agtv_rhs->type == AGTV_INTEGER)
    {
        if (agtv_rhs->val.int_value == 0)
            elog(ERROR, "division by zero");

        agtv_result.type = AGTV_FLOAT;
        agtv_result.val.float_value = agtv_lhs->val.float_value /
                                      agtv_rhs->val.int_value;
        AG_RETURN_AGTYPE_P(agtype_value_to_agtype(&agtv_result));
    }
    if (agtv_lhs->type == AGTV_INTEGER && agtv_rhs->type == AGTV_FLOAT)
    {
        if (agtv_rhs->val.float_value == 0)
            elog(ERROR, "division by zero");

        agtv_result.type = AGTV_FLOAT;
        agtv_result.val.float_value = agtv_lhs->val.int_value /
                                      agtv_rhs->val.float_value;
        AG_RETURN_AGTYPE_P(agtype_value_to_agtype(&agtv_result));
    }

    elog(ERROR, "opertion is not yet supported");

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
        elog(ERROR, "only scalar operations are supported");
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

    elog(ERROR, "opertion is not yet supported");

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
        elog(ERROR, "only scalar operations are supported");
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

    elog(ERROR, "opertion is not yet supported");

    PG_RETURN_NULL();
}
