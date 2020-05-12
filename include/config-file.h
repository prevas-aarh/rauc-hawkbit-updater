/**
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 * Copyright (C) 2018-2020 Prevas A/S (www.prevas.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef __CONFIG_FILE_H__
#define __CONFIG_FILE_H__

#include <glib.h>

/**
 * @brief struct that contains the Rauc HawkBit configuration.
 */
typedef struct Config_ {
        gchar* hawkbit_server;            /**< hawkBit host or IP and port */
        gboolean ssl;                     /**< use https or http */
        gboolean ssl_verify;              /**< verify https certificate */
        gboolean post_update_reboot;      /**< reboot system after successful update */
        gchar* auth_token;                /**< hawkBit target security token */
        gchar* gateway_token;             /**< hawkBit gateway security token */
        gchar* tenant_id;                 /**< hawkBit tenant id */
        gchar* controller_id;             /**< hawkBit controller id*/
        gchar* bundle_download_location;  /**< file to download rauc bundle to */
        gchar* client_cert;               /**< path to the client certificate */
        gchar* client_key;                /**< path to the key of the client certificate */
        int connect_timeout;              /**< connection timeout */
        int timeout;                      /**< reply timeout */
        int retry_wait;                   /**< wait between retries */
        GLogLevelFlags log_level;         /**< log level */
        GHashTable* device;               /**< Additional attributes sent to hawkBit */
} Config;

/**
 * @brief Get Config for config_file.
 *
 * @param[in]  config_file String value containing path to config file
 * @param[out] error       Error
 * @return Config on success, NULL otherwise (error is set)
 */
Config* load_config_file(const gchar *config_file, GError **error);

/**
 * @brief Frees the memory allocated by a Config
 *
 * @param[in] config Config to free
 */
void config_file_free(Config *config);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(Config, config_file_free)

#endif // __CONFIG_FILE_H__
