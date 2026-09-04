/*
 * Copyright 2022-2023 Morse Micro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <linux/timer.h>

#include <linux/bitfield.h>

#include "morse.h"
#include "mac.h"
#include "debug.h"

#define MORSE_CAC_DBG(_m, _f, _a...)	morse_dbg(FEATURE_ID_CAC, _m, _f, ##_a)
#define MORSE_CAC_INFO(_m, _f, _a...)	morse_info(FEATURE_ID_CAC, _m, _f, ##_a)
#define MORSE_CAC_WARN(_m, _f, _a...)	morse_warn(FEATURE_ID_CAC, _m, _f, ##_a)
#define MORSE_CAC_ERR(_m, _f, _a...)	morse_err(FEATURE_ID_CAC, _m, _f, ##_a)

/*
 * 802.11ah CAC (Centralized Authentication Control)
 * See IEEE 802.11REVme 9.4.2.202 and 11.3.9.2.
 */

#define MORSE_CAC_CHECK_INTERVAL_MS	100
#define MORSE_CAC_CHECK_PERIOD_MS	1000

struct cac_threshold_change_rules cac_threshold_change_rules_default = {
	6,
	{
		/* Increase threshold rules */
		{ 16, -255 },
		{ 12, -122 },
		{ 10, -61 },
		/* Decrease threshold rules */
		{ 4, 255 },
		{ 6, 122 },
		{ 8, 61 },
	}
};

void morse_cac_count_auth(const struct ieee80211_vif *vif, const struct ieee80211_mgmt *hdr)
{
	struct morse_vif *mors_vif = (struct morse_vif *)vif->drv_priv;
	struct morse_cac *cac = &mors_vif->cac;
	const u16 auth_transaction = le16_to_cpu(hdr->u.auth.auth_transaction);

	/* Ignore SAE auth that is already in progress */
	if (auth_transaction != 1)
		return;

	/* Don't include retries in calculations */
	if (ieee80211_has_retry(hdr->frame_control)) {
		cac->arrfs++;
		return;
	}

	cac->arfs++;
}

void morse_cac_count_probe_reqs(const struct ieee80211_vif *vif)
{
	struct morse_vif *mors_vif = (struct morse_vif *)vif->drv_priv;
	struct morse_cac *cac = &mors_vif->cac;

	cac->prfs++;
}

static void cac_threshold_change(struct morse_cac *cac, int diff)
{
	int threshold_value = cac->threshold_value + diff;

	if (threshold_value < 0)
		cac->threshold_value = 0;
	else if (threshold_value > CAC_THRESHOLD_MAX)
		cac->threshold_value = CAC_THRESHOLD_MAX;
	else
		cac->threshold_value = threshold_value;
}

#undef MORSE_CAC_TEST
#if defined MORSE_CAC_TEST

#include <linux/random.h>

#define CAC_TEST_PERIOD		(8)	/* Period in tenths of a second */
#define CAC_TEST_ARFS_MAX	(20)	/* Max random ARFS value */

/* Set ARFS to a random value */
void cac_test(struct morse_cac *cac)
{
	struct morse *mors = cac->mors;
	static int cnt;

	cnt++;
	if ((cnt % CAC_TEST_PERIOD) == 0) {
		u16 random;

		get_random_bytes(&random, sizeof(random));
		cac->arfs = random % CAC_TEST_ARFS_MAX;
		MORSE_CAC_INFO(mors, "CAC: TEST set ARFS to %u\n", cac->arfs);
	}
}

#else
#define cac_test(_cac)
#endif

/**
 * @brief Adjust the CAC threshold based on frequency of Rx authentication frames
 *
 * If the number of authentication frames received within the checking interval
 * exceeds predefined thresholds, reduce the CAC threshold in order to reduce the
 * number of stations that are allowed to start association.
 *
 * This check is performed many times per second in order to react quickly to a
 * surge in associations (E.g. after an AP or network restart). If the threshold
 * is increased, the checking period is restarted.
 *
 * If the end of the checking period is reached and only a small number of stations
 * have associated, the CAC threshold is increased (relaxed).
 */
static int cac_set_threshold_change(struct morse_cac *cac, bool end_of_period)
{
	struct morse *mors = cac->mors;
	int i;

	if (!cac->conf)
		return -ENOENT;

	for (i = 0; i < cac->conf->rules.rule_tot; i++) {
		struct cac_threshold_change_rule *rule = &cac->conf->rules.rule[i];

		if (rule->threshold_change < 0) {
			/* Process rule to decrease threshold */
			if (cac->arfs > rule->arfs) {
				/* Decrease threshold */
				MORSE_CAC_DBG(mors, "CAC: Apply rule %i arfs>%u change=%d\n",
					i, rule->arfs, rule->threshold_change);
				return rule->threshold_change;
			}
		} else {
			/* Process rule to increase threshold */
			if (cac->threshold_value == CAC_THRESHOLD_MAX)
				/* Already at max */
				return 0;
			if (!end_of_period)
				/* Only increase at the end of a sample period */
				return 0;
			if (cac->arfs < rule->arfs) {
				/* Increase threshold */
				MORSE_CAC_DBG(mors, "CAC: Apply rule %i arfs<%u change=%d\n",
					i, rule->arfs, rule->threshold_change);
				return rule->threshold_change;
			}
		}
	}

	return 0;
}

static void cac_timer_work(struct morse_cac *cac)
{
	struct morse *mors = cac->mors;
	int threshold_change = 0;
	bool end_of_period = false;

	if (!cac->conf || !cac->conf->enabled)
		return;

	cac_test(cac);

	cac->cac_period_used += MORSE_CAC_CHECK_INTERVAL_MS;
	if (cac->cac_period_used >= MORSE_CAC_CHECK_PERIOD_MS)
		end_of_period = true;

	/* Check if the threshold needs to be tighted or relaxed and set. */
	if (cac->arfs != 0 || cac->threshold_value != CAC_THRESHOLD_MAX) {
		MORSE_CAC_DBG(mors,
			      "CAC: Check ARFS=%u (ARRFS=%u) threshold=%u end=%u probes=%u\n",
			      cac->arfs, cac->arrfs, cac->threshold_value, end_of_period,
			      cac->prfs);
		threshold_change = cac_set_threshold_change(cac, end_of_period);
		if (threshold_change != 0) {
			cac_threshold_change(cac, threshold_change);
			MORSE_CAC_INFO(mors, "CAC: Set threshold %u (period=%u)\n",
				cac->threshold_value, cac->cac_period_used);
			end_of_period = true;
		}
	}

	if (end_of_period) {
		cac->cac_period_used = 0;
		cac->arfs = 0;
		cac->arrfs = 0;
		cac->prfs = 0;
	}

	mod_timer(&cac->timer, jiffies + msecs_to_jiffies(MORSE_CAC_CHECK_INTERVAL_MS));
}

#if KERNEL_VERSION(4, 15, 0) > LINUX_VERSION_CODE
static void cac_timer(unsigned long addr)
#else
static void cac_timer(struct timer_list *t)
#endif
{
#if KERNEL_VERSION(4, 15, 0) > LINUX_VERSION_CODE
	struct morse_cac *cac = (struct morse_cac *)addr;
#else
	struct morse_cac *cac = TIMER_TO_OBJ(cac, t, timer);
#endif

	spin_lock_bh(&cac->lock);

	cac_timer_work(cac);

	spin_unlock_bh(&cac->lock);
}

void morse_cac_insert_ie(struct dot11ah_ies_mask *ies_mask, struct ieee80211_vif *vif, __le16 fc)
{
	struct morse_vif *mors_vif = ieee80211_vif_to_morse_vif(vif);
	struct dot11ah_s1g_auth_control_ie cac_ie = { 0 };
	struct morse_cac *cac = &mors_vif->cac;

	if (!cac->conf || !cac->conf->enabled)
		return;

	/* At the moment only apply to Probe Response and Beacon frames */
	if (!ieee80211_is_probe_resp(fc) && !ieee80211_is_beacon(fc))
		return;

	cac_ie.parameters = FIELD_PREP(DOT11AH_S1G_CAC_THRESHOLD, mors_vif->cac.threshold_value);

	morse_dot11ah_insert_element(ies_mask, WLAN_EID_S1G_CAC, (u8 *)&cac_ie, sizeof(cac_ie));
}

bool morse_cac_is_enabled(struct morse_vif *mors_vif)
{
	struct morse_cac *cac = &mors_vif->cac;

	return cac->conf && cac->conf->enabled;
}

int morse_cac_deinit(struct morse_vif *mors_vif)
{
	struct morse_cac *cac = &mors_vif->cac;

	if (!cac->conf || !cac->conf->enabled)
		return 0;

	cac->conf = NULL;
	if (!mors_vif->ap)
		return 0;

	DEL_TIMER_SYNC(&cac->timer);

	return 0;
}

static void morse_cac_cfg_threshold_rules_default(struct morse *mors, struct morse_vif *mors_vif)
{
	struct morse_cac *cac = &mors_vif->cac;

	if (!cac->conf)
		return;

	spin_lock_bh(&cac->lock);

	memcpy(&cac->conf->rules, &cac_threshold_change_rules_default, sizeof(cac->conf->rules));

	spin_unlock_bh(&cac->lock);
}

void morse_cac_get_rules(struct morse_vif *mors_vif, struct cac_threshold_change_rules *rules,
			 u8 *rule_tot)
{
	struct morse_cac *cac = &mors_vif->cac;

	if (!cac->conf)
		return;

	spin_lock_bh(&cac->lock);

	memcpy(rules, &cac->conf->rules, sizeof(cac->conf->rules));

	spin_unlock_bh(&cac->lock);
}

void morse_cac_set_rules(struct morse_vif *mors_vif, struct cac_threshold_change_rules *rules)
{
	struct morse_cac *cac = &mors_vif->cac;

	if (!cac->conf)
		return;

	spin_lock_bh(&cac->lock);

	memcpy(&cac->conf->rules, rules, sizeof(cac->conf->rules));

	spin_unlock_bh(&cac->lock);
}

void morse_cac_reconfig(struct morse *mors, struct morse_vif *mors_vif)
{
	struct morse_persistent_vif_configs *mors_vif_conf =
					morse_get_vif_conf_from_id(mors, mors_vif->id);
	struct morse_cac *cac = &mors_vif->cac;

	lockdep_assert_held(&mors->persistent_vif_config.lock);

	cac->conf = &mors_vif_conf->cac_conf;
}

int morse_cac_init(struct morse *mors, struct morse_vif *mors_vif, bool in_reconfig)
{
	struct morse_persistent_vif_configs *mors_vif_conf;
	struct morse_cac *cac = &mors_vif->cac;
	struct ieee80211_vif *vif = morse_vif_to_ieee80211_vif(mors_vif);
	int ret = 0;

	mutex_lock(&mors->persistent_vif_config.lock);

	mors_vif_conf = morse_get_vif_conf_from_addr(mors, vif->addr);
	if (!mors_vif_conf) {
		ret = -ENXIO;
		goto exit;
	}

	cac->conf = &mors_vif_conf->cac_conf;

	/* CAC config not enabled before trying to reconfig or has already been
	 * configured while being in ON state.
	 */
	if ((in_reconfig && !cac->conf->enabled) || (!in_reconfig && cac->conf->enabled))
		goto exit;

	if (!in_reconfig)
		memset(cac->conf, 0, sizeof(*cac->conf));

	if (vif->type == NL80211_IFTYPE_STATION) {
		cac->conf->enabled = 1;
		goto exit;
	}

	if (vif->type != NL80211_IFTYPE_AP)
		goto exit;

	spin_lock_init(&cac->lock);

	cac->mors = mors;

#if KERNEL_VERSION(4, 15, 0) > LINUX_VERSION_CODE
	init_timer(&cac->timer);
	cac->timer.data = (unsigned long)cac;
	cac->timer.function = cac_timer;
	add_timer(&cac->timer);
#else
	timer_setup(&cac->timer, cac_timer, 0);
#endif

	mod_timer(&cac->timer, jiffies + msecs_to_jiffies(MORSE_CAC_CHECK_INTERVAL_MS));
	cac->threshold_value = CAC_THRESHOLD_MAX;
	if (!in_reconfig) {
		cac->conf->enabled = 1;
		morse_cac_cfg_threshold_rules_default(mors, mors_vif);
	}

exit:
	mutex_unlock(&mors->persistent_vif_config.lock);
	return ret;
}
