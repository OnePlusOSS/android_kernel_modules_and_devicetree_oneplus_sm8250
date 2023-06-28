/*
 * Copyright (c) 2017-2020 The Linux Foundation. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __WLAN_HDD_RX_MONITOR_H
#define __WLAN_HDD_RX_MONITOR_H

struct ol_txrx_ops;

#if defined(QCA_WIFI_QCA6290) || defined(QCA_WIFI_QCA6390) || \
    defined(QCA_WIFI_QCA6490) || defined(QCA_WIFI_QCA6750)
void hdd_monitor_set_rx_monitor_cb(struct ol_txrx_ops *txrx,
				ol_txrx_rx_mon_fp rx_monitor_cb);

void hdd_rx_monitor_callback(ol_osif_vdev_handle vdev,
				qdf_nbuf_t mpdu,
				void *rx_status);

int hdd_enable_monitor_mode(struct net_device *dev);
#else
static inline void hdd_monitor_set_rx_monitor_cb(struct ol_txrx_ops *txrx,
					ol_txrx_rx_mon_fp rx_monitor_cb){ }
static inline void hdd_rx_monitor_callback(ol_osif_vdev_handle vdev,
				qdf_nbuf_t mpdu,
				void *rx_status){ }
static inline int hdd_enable_monitor_mode(struct net_device *dev)
{
	return 0;
}
#endif /* CONFIG_LITHIUM */

#endif /* __WLAN_HDD_RX_MONITOR_H */

