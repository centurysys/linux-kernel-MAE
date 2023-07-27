/*
 * Copyright 2022 Morse Micro
 */
#include <linux/timer.h>

#if defined MORSE_CAC_TEST
#include <linux/random.h>
#endif

#include "morse.h"
#include "mac.h"
#include "debug.h"
#include "mac.h"

/*
 * 802.11ah CAC (Centralised Authentication Control)
 * See IEEE 802.11REVme 9.4.2.202 and 11.3.9.2.
 */

#define MORSE_CAC_CHECK_INTERVAL_MS	100
#define MORSE_CAC_CHECK_PERIOD_MS	1000

void morse_cac_count_auth(const struct ieee80211_vif *vif,
	const struct ieee80211_mgmt *hdr, size_t len)
{
	struct morse_vif *mors_if = (struct morse_vif *)vif->drv_priv;
	struct morse_cac *cac = &mors_if->cac;
	const u16 auth_transaction = le16_to_cpu(hdr->u.auth.auth_transaction);

	/* Ignore SAE auth that is already in progress */
	if (auth_transaction != 1)
		return;

	cac->arfs++;
}

static void cac_threshold_change(struct morse_cac *cac, int diff)
{
	int threshold_index = cac->threshold_index + diff;

	if (threshold_index < 0)
		cac->threshold_index = 0;
	else if (threshold_index > CAC_INDEX_MAX)
		cac->threshold_index = CAC_INDEX_MAX;
	else
		cac->threshold_index = threshold_index;
}

/**
 * @brief Adjust the CAC threshold based on frequency of Rx authentication frames
 *
 * If the number of authentication frames received within the checking interval
 * exceeds predefined thresholds, reduce the CAC threshold in order to reduce the
 * number of stations which are allowed to start association.
 *
 * This check is performed many times per second in order to react quickly to a
 * surge in associations (E.g. after an AP or network restart). If the threshold
 * is increased, the checking period is restarted.
 *
 * If the end of the checking period is reached and only a small number of stations
 * have associated, the CAC threshold is increased (relaxed).
 */
static void cac_timer_work(struct morse_cac *cac)
{
	struct morse *mors  = cac->mors;
	int threshold_change = 0;
	bool end_of_period = false;

	if (!cac->enabled)
		return;

	cac->cac_period_used += MORSE_CAC_CHECK_INTERVAL_MS;
	if (cac->cac_period_used >= MORSE_CAC_CHECK_PERIOD_MS)
		end_of_period = true;

	/*
	 * If there are too many authentication requests, reduce the threshold.
	 * If CAC threshold is not at min and there have been few authentication
	 * requests, increase the threshold.
	 */
	if (cac->arfs > 16) {
		threshold_change = -4;
	} else if (cac->arfs > 12) {
		threshold_change = -2;
	} else if (cac->arfs > 10) {
		threshold_change = -1;
	} else if ((cac->threshold_index < CAC_INDEX_MAX) && end_of_period) {
		if (cac->arfs <= 4)
			threshold_change = 4;
		else if (cac->arfs <= 6)
			threshold_change = 2;
		else if (cac->arfs <= 8)
			threshold_change = 1;
	}

	if (threshold_change != 0) {
		cac_threshold_change(cac, threshold_change);
		MORSE_CAC_PRINT(mors,
			"CAC ARFS=%u period=%u adjust=%d idx=%u threshold=%u\n",
			cac->arfs,
			cac->cac_period_used,
			threshold_change, cac->threshold_index,
			cac->threshold_index * CAC_THRESHOLD_STEP);
		end_of_period = true;
	}

	if (end_of_period) {
		cac->cac_period_used = 0;
		cac->arfs = 0;
	}

#if defined MORSE_CAC_TEST
	{
		static int cnt;

		if ((cnt++ % 16) == 0) {
			u16 random;

			get_random_bytes(&random, sizeof(random));
			cac->threshold_index = random % 8;
			MORSE_CAC_PRINT(mors, "CAC TESTING change index to %u\n",
				cac->threshold_index);
		}
	}
#endif

	mod_timer(&cac->timer, jiffies + msecs_to_jiffies(MORSE_CAC_CHECK_INTERVAL_MS));
}

#if KERNEL_VERSION(4, 14, 0) >= LINUX_VERSION_CODE
static void cac_timer(unsigned long addr)
#else
static void cac_timer(struct timer_list *t)
#endif
{
#if KERNEL_VERSION(4, 14, 0) >= LINUX_VERSION_CODE
	struct morse_cac *cac = (struct morse_cac *) addr;
#else
	struct morse_cac *cac = from_timer(cac, t, timer);
#endif

	spin_lock_bh(&cac->lock);

	cac_timer_work(cac);

	spin_unlock_bh(&cac->lock);
}

int morse_cac_deinit(const struct morse *mors)
{
	struct ieee80211_vif *vif = morse_get_ap_vif((struct morse *)mors);
	struct morse_vif *mors_if = NULL;
	struct morse_cac *cac = NULL;

	if (vif == NULL)
		return 0;

	mors_if = (struct morse_vif *)vif->drv_priv;
	cac = &mors_if->cac;

	if (!cac->enabled)
		return 0;

	cac->enabled = 0;

	spin_lock_bh(&cac->lock);

	del_timer_sync(&cac->timer);

	spin_unlock_bh(&cac->lock);

	return 0;
}

int morse_cac_init(struct morse *mors)
{
	struct ieee80211_vif *vif = morse_get_ap_vif(mors);
	struct morse_vif *mors_if = NULL;
	struct morse_cac *cac = NULL;

	if (vif == NULL || vif->type != NL80211_IFTYPE_AP)
		return 0;

	mors_if = (struct morse_vif *)vif->drv_priv;
	cac = &mors_if->cac;

	spin_lock_init(&cac->lock);

	cac->mors = mors;

#if KERNEL_VERSION(4, 14, 0) >= LINUX_VERSION_CODE
	init_timer(&cac->timer);
	cac->timer.data = (unsigned long) &cac;
	cac->timer.function = cac_timer;
	add_timer(&cac->timer);
#else
	timer_setup(&cac->timer, cac_timer, 0);
#endif

	mod_timer(&cac->timer, jiffies + msecs_to_jiffies(MORSE_CAC_CHECK_INTERVAL_MS));
	cac->threshold_index = CAC_INDEX_MAX;
	cac->enabled = 1;

	return 0;
}

