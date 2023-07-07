/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>

#include <thingset.h>
#include <thingset/sdk.h>

LOG_MODULE_REGISTER(thingset_auth, CONFIG_LOG_DEFAULT_LEVEL);

static char auth_token[CONFIG_THINGSET_AUTH_TOKEN_MAX_SIZE];

static int32_t thingset_auth();

THINGSET_ADD_FN_INT32(TS_ID_ROOT, TS_ID_AUTH, "xAuth", &thingset_auth, THINGSET_ANY_RW);
THINGSET_ADD_ITEM_STRING(TS_ID_AUTH, TS_ID_AUTH_TOKEN, "uToken", auth_token, sizeof(auth_token),
                         THINGSET_ANY_RW, 0);

static int thingset_auth()
{
    static const char token_exp[] = CONFIG_THINGSET_AUTH_TOKEN_EXPERT;
    static const char token_mfr[] = CONFIG_THINGSET_AUTH_TOKEN_MANUFACTURER;

    if (strlen(token_exp) == strlen(auth_token)
        && strncmp(auth_token, token_exp, strlen(token_exp)) == 0)
    {
        thingset_set_authentication(&ts, THINGSET_EXP_MASK | THINGSET_USR_MASK);
        LOG_INF("Authenticated as expert user");
        return 0;
    }
    else if (strlen(token_mfr) == strlen(auth_token)
             && strncmp(auth_token, token_mfr, strlen(token_mfr)) == 0)
    {
        thingset_set_authentication(&ts, THINGSET_MFR_MASK | THINGSET_USR_MASK);
        LOG_INF("Authenticated as manufacturer");
        return 0;
    }
    else {
        thingset_set_authentication(&ts, THINGSET_USR_MASK);
        LOG_INF("Authentication reset");
        return -EINVAL;
    }
}
