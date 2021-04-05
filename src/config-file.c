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
 * @file config-file.c
 * @author Lasse Mikkelsen <lkmi@prevas.dk>
 * @date 19 Sep 2018
 * @brief Configuration file parser
 *
 */

#include "config-file.h"
#include <glib/gtypes.h>
#include <stdlib.h>
#include <libeconf.h>

G_DEFINE_AUTOPTR_CLEANUP_FUNC(econf_file, econf_freeFile)


static const gchar *CONFIG_DELIMITER      = "=";
static const gchar *CONFIG_COMMENT        = "#";
static const gint DEFAULT_CONNECTTIMEOUT  = 20;     // 20 sec.
static const gint DEFAULT_TIMEOUT         = 60;     // 1 min.
static const gint DEFAULT_RETRY_WAIT      = 5 * 60; // 5 min.
static const gboolean DEFAULT_SSL         = TRUE;
static const gboolean DEFAULT_SSL_VERIFY  = TRUE;
static const gboolean DEFAULT_REBOOT      = FALSE;
static const gchar* DEFAULT_LOG_LEVEL     = "message";

/**
 * @brief Get string value from key_file for key in group, optional default_value can be specified
 * that will be used in case key is not found in group.
 *
 * @param[in]  key_file      econf_file to look value up
 * @param[in]  group         A group name
 * @param[in]  key           A key
 * @param[out] value         Output string value
 * @param[in]  default_value String value to return in case no value found, or NULL (not found
 *                           leads to error)
 * @param[out] error         Error
 * @return TRUE if found, TRUE if not found and default_value given, FALSE otherwise (error is set)
 */
static gboolean get_key_string(econf_file *key_file, const gchar *group, const gchar *key,
                               gchar **value, const gchar *default_value, GError **error)
{
        g_autofree gchar *default_nonconst = g_strdup(default_value);
        econf_err econf_error;

        g_return_val_if_fail(key_file, FALSE);
        g_return_val_if_fail(group, FALSE);
        g_return_val_if_fail(key, FALSE);
        g_return_val_if_fail(value && *value == NULL, FALSE);
        g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

        if (default_value)
                econf_error = econf_getStringValueDef(key_file, group, key, value,
                                                      default_nonconst);
        else
                econf_error = econf_getStringValue(key_file, group, key, value);

        if (!econf_error)
                return TRUE;
        if (default_value && econf_error == ECONF_NOKEY)
                return TRUE;

        g_set_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_PARSE,
                    "Key '%s' (string, group '%s'): %s", key, group, econf_errString(econf_error));
        return FALSE;
}

/**
 * @brief Get gboolean value from key_file for key in group, default_value must be specified,
 * returned in case key not found in group.
 *
 * @param[in]  key_file      econf_file to look value up
 * @param[in]  group         A group name
 * @param[in]  key           A key
 * @param[out] value         Output gboolean value
 * @param[in]  default_value Return this value in case no value found
 * @param[out] error         Error
 * @return FALSE on error (error is set), TRUE otherwise. Note that TRUE is returned if key in
 *         group is not found, value is set to default_value in this case.
 */
static gboolean get_key_bool(econf_file *key_file, const gchar *group, const gchar *key,
                             gboolean *value, const gboolean default_value, GError **error)
{
        econf_err econf_error;

        g_return_val_if_fail(key_file, FALSE);
        g_return_val_if_fail(group, FALSE);
        g_return_val_if_fail(key, FALSE);
        g_return_val_if_fail(value, FALSE);
        g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

        econf_error = econf_getBoolValueDef(key_file, group, key, (bool *)value, default_value);
        if (econf_error && econf_error != ECONF_NOKEY) {
                g_set_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_PARSE,
                            "Key '%s' (bool, group '%s'): %s", key, group,
                            econf_errString(econf_error));
                return FALSE;
        }

        return TRUE;
}

/**
 * @brief Get integer value from key_file for key in group, default_value must be specified,
 * returned in case key not found in group.
 *
 * @param[in]  key_file      econf_file to look value up
 * @param[in]  group         A group name
 * @param[in]  key           A key
 * @param[out] value         Output integer value
 * @param[in]  default_value Return this value in case no value found
 * @param[out] error         Error
 * @return FALSE on error (error is set), TRUE otherwise. Note that TRUE is returned if key in
 *         group is not found, value is set to default_value in this case.
 */
static gboolean get_key_int(econf_file *key_file, const gchar *group, const gchar *key,
                            gint *value, const gint default_value, GError **error)
{
        econf_err econf_error;

        g_return_val_if_fail(key_file, FALSE);
        g_return_val_if_fail(group, FALSE);
        g_return_val_if_fail(key, FALSE);
        g_return_val_if_fail(value, FALSE);
        g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

        econf_error = econf_getIntValueDef(key_file, group, key, value, default_value);
        if (econf_error && econf_error != ECONF_NOKEY) {
                g_set_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_PARSE,
                            "Key '%s' (int, group '%s'): %s", key, group,
                            econf_errString(econf_error));
                return FALSE;
        }

        return TRUE;
}

/**
 * @brief Get GHashTable containing keys/values from group in key_file.
 *
 * @param[in]  key_file econf_file to look value up
 * @param[in]  group    A group name
 * @param[out] hash     Output GHashTable
 * @param[out] error    Error
 * @return TRUE on keys/values stored successfully, FALSE on empty group/value or on other errors
 *         (error set)
 */
static gboolean get_group(econf_file *key_file, const gchar *group, GHashTable **hash,
                          GError **error)
{
        g_autoptr(GHashTable) tmp_hash = NULL;
        size_t key, num_keys = 0;
        g_auto(GStrv) keys = NULL;
        econf_err econf_error;

        g_return_val_if_fail(key_file, FALSE);
        g_return_val_if_fail(group, FALSE);
        g_return_val_if_fail(hash && *hash == NULL, FALSE);
        g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

        tmp_hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

        econf_error = econf_getKeys(key_file, group, &num_keys, &keys);
        if (econf_error) {
                g_set_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_PARSE,
                            "Enumerating keys in group '%s' failed: %s", group,
                            econf_errString(econf_error));
                return FALSE;
        }

        for (key = 0; key < num_keys; key++) {
                gchar *value = NULL;
                if (!get_key_string(key_file, group, keys[key], &value, NULL, error))
                        return FALSE;

                g_hash_table_insert(tmp_hash, g_strdup(keys[key]), value);
        }

        *hash = g_steal_pointer(&tmp_hash);
        return TRUE;
}

/**
 * @brief Get GLogLevelFlags for error string.
 *
 * @param[in]  log_level Log level string
 * @return GLogLevelFlags matching error string, else default log level (message)
 */
static GLogLevelFlags log_level_from_string(const gchar *log_level)
{
        g_return_val_if_fail(log_level, 0);

        if (g_strcmp0(log_level, "error") == 0) {
                return G_LOG_LEVEL_ERROR;
        } else if (g_strcmp0(log_level, "critical") == 0) {
                return G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL;
        } else if (g_strcmp0(log_level, "warning") == 0) {
                return G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL |
                       G_LOG_LEVEL_WARNING;
        } else if (g_strcmp0(log_level, "message") == 0) {
                return G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL |
                       G_LOG_LEVEL_WARNING | G_LOG_LEVEL_MESSAGE;
        } else if (g_strcmp0(log_level, "info") == 0) {
                return G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL |
                       G_LOG_LEVEL_WARNING | G_LOG_LEVEL_MESSAGE |
                       G_LOG_LEVEL_INFO;
        } else if (g_strcmp0(log_level, "debug") == 0) {
                return G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL |
                       G_LOG_LEVEL_WARNING | G_LOG_LEVEL_MESSAGE |
                       G_LOG_LEVEL_INFO | G_LOG_LEVEL_DEBUG;
        } else {
                g_warning("Invalid log level given, defaulting to level \"message\"");
                return G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL |
                       G_LOG_LEVEL_WARNING | G_LOG_LEVEL_MESSAGE;
        }
}

Config* load_config_file(const gchar *config_file, GError **error)
{
        g_autoptr(Config) config = NULL;
        g_autofree gchar *val = NULL;
        g_autoptr(econf_file) ini_file = NULL;
        gboolean key_auth_token_exists = FALSE;
        gboolean key_gateway_token_exists = FALSE;
        econf_err econf_error;

        g_return_val_if_fail(config_file, NULL);
        g_return_val_if_fail(error == NULL || *error == NULL, NULL);

        config = g_new0(Config, 1);

        econf_error = econf_readFile(&ini_file, config_file, CONFIG_DELIMITER, CONFIG_COMMENT);
        if (econf_error) {
                g_set_error_literal(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_PARSE,
                                    econf_errString(econf_error));
                return NULL;
        }

        if (!get_key_string(ini_file, "client", "hawkbit_server", &config->hawkbit_server, NULL,
                            error))
                return NULL;

        key_auth_token_exists = get_key_string(ini_file, "client", "auth_token",
                                               &config->auth_token, NULL, NULL);
        key_gateway_token_exists = get_key_string(ini_file, "client", "gateway_token",
                                                  &config->gateway_token, NULL, NULL);
        if (!key_auth_token_exists && !key_gateway_token_exists) {
                g_set_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                            "Neither auth_token nor gateway_token is set in the config.");
                return NULL;
        }
        if (key_auth_token_exists && key_gateway_token_exists) {
                g_set_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                            "Both auth_token and gateway_token are set in the config.");
                return NULL;
        }

        if (!get_key_string(ini_file, "client", "target_name", &config->controller_id, NULL,
                            error))
                return NULL;
        if (!get_key_string(ini_file, "client", "tenant_id", &config->tenant_id, "DEFAULT", error))
                return NULL;
        if (!get_key_string(ini_file, "client", "bundle_download_location",
                            &config->bundle_download_location, NULL, error))
                return NULL;
        if (!get_key_bool(ini_file, "client", "ssl", &config->ssl, DEFAULT_SSL, error))
                return NULL;
        if (!get_key_bool(ini_file, "client", "ssl_verify", &config->ssl_verify,
                          DEFAULT_SSL_VERIFY, error))
                return NULL;
        if (!get_key_int(ini_file, "client", "connect_timeout", &config->connect_timeout,
                         DEFAULT_CONNECTTIMEOUT, error))
                return NULL;
        if (!get_key_int(ini_file, "client", "timeout", &config->timeout, DEFAULT_TIMEOUT, error))
                return NULL;
        if (!get_key_int(ini_file, "client", "retry_wait", &config->retry_wait, DEFAULT_RETRY_WAIT,
                         error))
                return NULL;
        if (!get_key_string(ini_file, "client", "log_level", &val, DEFAULT_LOG_LEVEL, error))
                return NULL;
        config->log_level = log_level_from_string(val);

        if (!get_key_bool(ini_file, "client", "post_update_reboot", &config->post_update_reboot, DEFAULT_REBOOT, error))
                return NULL;

        if (config->timeout > 0 && config->connect_timeout > 0 &&
            config->timeout < config->connect_timeout) {
                g_set_error(error,
                            G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                            "timeout (%d) must be greater than connect_timeout (%d)",
                            config->timeout, config->connect_timeout);
                return NULL;
        }

        if (!get_group(ini_file, "device", &config->device, error))
                return NULL;

        return g_steal_pointer(&config);
}

void config_file_free(Config *config)
{
        if (!config)
                return;

        g_free(config->hawkbit_server);
        g_free(config->controller_id);
        g_free(config->tenant_id);
        g_free(config->auth_token);
        g_free(config->gateway_token);
        g_free(config->bundle_download_location);
        if (config->device)
                g_hash_table_destroy(config->device);
        g_free(config);
}
