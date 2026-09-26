/**
 * @file acm.c
 * @implements SRS-ACM-001
 */
#include "satlink/acm/acm.h"

#include <stddef.h>

#include "satlink/modem/modcod.h"

satlink_acm_config_t satlink_acm_default_config(void)
{
    satlink_acm_config_t c;
    c.min_modcod = 0U;
    c.max_modcod = (uint8_t)(SATLINK_MODCOD_COUNT - 1U);
    c.margin_db = 1.0F;
    c.hysteresis_db = 1.0F;
    c.up_hold = 3U;
    return c;
}

static bool config_valid(const satlink_acm_config_t *c)
{
    return (c->min_modcod <= c->max_modcod) && (c->max_modcod < SATLINK_MODCOD_COUNT) &&
           (c->margin_db >= 0.0F) && (c->hysteresis_db >= 0.0F) && (c->up_hold > 0U);
}

static float threshold(uint8_t modcod)
{
    return satlink_modcod_get(modcod)->esn0_threshold_db;
}

static void change(satlink_acm_t *acm, uint8_t to, satlink_acm_reason_t reason, float esn0_db)
{
    const uint8_t from = acm->current;
    acm->current = to;
    acm->good_count = 0U;
    if (reason == SATLINK_ACM_UP)
    {
        ++acm->changes_up;
    }
    else
    {
        ++acm->changes_down;
    }
    if (acm->on_change != NULL)
    {
        acm->on_change(acm->ctx, from, to, reason, esn0_db);
    }
}

satlink_status_t satlink_acm_init(satlink_acm_t *acm, const satlink_acm_config_t *config,
                                  satlink_acm_event_fn on_change, void *ctx)
{
    if ((acm == NULL) || (config == NULL))
    {
        return SATLINK_ERR_NULL;
    }
    if (!config_valid(config))
    {
        return SATLINK_ERR_RANGE;
    }
    acm->config = *config;
    acm->current = config->min_modcod;
    acm->good_count = 0U;
    acm->changes_up = 0U;
    acm->changes_down = 0U;
    acm->decisions = 0U;
    acm->on_change = on_change;
    acm->ctx = ctx;
    return SATLINK_OK;
}

satlink_status_t satlink_acm_configure(satlink_acm_t *acm, const satlink_acm_config_t *config)
{
    if ((acm == NULL) || (config == NULL))
    {
        return SATLINK_ERR_NULL;
    }
    if (!config_valid(config))
    {
        return SATLINK_ERR_RANGE;
    }
    acm->config = *config;
    if (acm->current < config->min_modcod)
    {
        acm->current = config->min_modcod;
    }
    if (acm->current > config->max_modcod)
    {
        acm->current = config->max_modcod;
    }
    acm->good_count = 0U;
    return SATLINK_OK;
}

uint8_t satlink_acm_target(const satlink_acm_t *acm, float esn0_db)
{
    uint8_t target = acm->config.min_modcod;
    for (uint8_t m = acm->config.min_modcod; m <= acm->config.max_modcod; ++m)
    {
        if ((threshold(m) + acm->config.margin_db) <= esn0_db)
        {
            target = m;
        }
    }
    return target;
}

uint8_t satlink_acm_update(satlink_acm_t *acm, float esn0_db, bool locked)
{
    ++acm->decisions;
    const satlink_acm_config_t *c = &acm->config;

    if (!locked)
    {
        if (acm->current != c->min_modcod)
        {
            change(acm, c->min_modcod, SATLINK_ACM_LOCK_LOST, esn0_db);
        }
        acm->good_count = 0U;
        return acm->current;
    }

    if (esn0_db < (threshold(acm->current) + c->margin_db))
    {
        const uint8_t target = satlink_acm_target(acm, esn0_db);
        if (target < acm->current)
        {
            change(acm, target, SATLINK_ACM_DOWN, esn0_db);
        }
        acm->good_count = 0U;
        return acm->current;
    }

    if (acm->current < c->max_modcod)
    {
        const uint8_t next = (uint8_t)(acm->current + 1U);
        if (esn0_db >= (threshold(next) + c->margin_db + c->hysteresis_db))
        {
            ++acm->good_count;
            if (acm->good_count >= c->up_hold)
            {
                change(acm, next, SATLINK_ACM_UP, esn0_db);
            }
        }
        else
        {
            acm->good_count = 0U;
        }
    }
    return acm->current;
}
