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
 * @file rauc-hawkbit-updater.c
 * @author Lasse Mikkelsen <lkmi@prevas.dk>
 * @date 19 Sep 2018
 * @brief RAUC HawkBit updater daemon
 *
 */


#include <stdio.h>
#include <glib.h>
#include <glib/gprintf.h>
#include "rauc-installer.h"
#include "hawkbit-client.h"
#include "config-file.h"
#include "log.h"

#define PROGRAM "rauc-hawkbit-updater"
#define VERSION 0.1

// program arguments
static gchar *config_file          = NULL;
static gboolean opt_version        = FALSE;
static gboolean opt_debug          = FALSE;
static gboolean opt_run_once       = FALSE;
static gboolean opt_output_systemd = FALSE;

// Commandline options
static GOptionEntry entries[] =
{
        { "config-file",      'c', G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &config_file,           "Configuration file",                       NULL },
        { "version",          'v', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,     &opt_version,           "Version information",                      NULL },
        { "debug",            'd', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,     &opt_debug,             "Enable debug output",                      NULL },
        { "run-once",         'r', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,     &opt_run_once,          "Check and install new software and exit",  NULL },
#ifdef WITH_SYSTEMD
        { "output-systemd",   's', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,     &opt_output_systemd,    "Enable output to systemd",                 NULL },
#endif
        { NULL }
};

// hawkbit callbacks
static GSourceFunc notify_hawkbit_install_progress;
static GSourceFunc notify_hawkbit_install_complete;


/**
 * @brief GSourceFunc callback for install thread, consumes RAUC progress messages, logs them and
 * passes them on to notify_hawkbit_install_progress().
 *
 * @param[in] data install_context pointer allowing access to received status messages
 * @return G_SOURCE_REMOVE is always returned
 */
static gboolean on_rauc_install_progress_cb(gpointer data)
{
        struct install_context *context = data;

        g_return_val_if_fail(data, G_SOURCE_REMOVE);

        g_mutex_lock(&context->status_mutex);
        while (!g_queue_is_empty(&context->status_messages)) {
                g_autofree gchar *msg = g_queue_pop_head(&context->status_messages);
                g_message("Installing: %s : %s", context->bundle, msg);
                // notify hawkbit server about progress
                notify_hawkbit_install_progress(msg);
        }
        g_mutex_unlock(&context->status_mutex);

        return G_SOURCE_REMOVE;
}

/**
 * @brief GSourceFunc callback for install thread, consumes RAUC installation status result
 *        (on complete) and passes it on to notify_hawkbit_install_complete().
 *
 * @param[in] data install_context pointer allowing access to received status result
 * @return G_SOURCE_REMOVE is always returned
 */
static gboolean on_rauc_install_complete_cb(gpointer data)
{
        struct install_context *context = data;
        struct on_install_complete_userdata userdata;

        g_return_val_if_fail(data, G_SOURCE_REMOVE);

        userdata.install_success = (context->status_result == 0);

        // notify hawkbit about install result
        notify_hawkbit_install_complete(&userdata);

        return G_SOURCE_REMOVE;
}

/**
 * @brief GSourceFunc callback for download thread, triggers RAUC installation.
 *
 * @param[in] data on_new_software_userdata pointer
 * @return G_SOURCE_REMOVE is always returned
 */
static gboolean on_new_software_ready_cb(gpointer data)
{
        struct on_new_software_userdata *userdata = data;

        g_return_val_if_fail(data, G_SOURCE_REMOVE);

        notify_hawkbit_install_progress = userdata->install_progress_callback;
        notify_hawkbit_install_complete = userdata->install_complete_callback;
        userdata->install_success = rauc_install(userdata->file, on_rauc_install_progress_cb,
                                                 on_rauc_install_complete_cb, run_once);

        return G_SOURCE_REMOVE;
}

int main(int argc, char **argv)
{
        g_autoptr(GError) error = NULL;
        g_autoptr(GOptionContext) context = NULL;
        g_auto(GStrv) args = NULL;
        GLogLevelFlags log_level;
        g_autoptr(Config) config = NULL;
        GLogLevelFlags fatal_mask;

        fatal_mask = g_log_set_always_fatal(G_LOG_FATAL_MASK);
        fatal_mask |= G_LOG_LEVEL_CRITICAL;
        g_log_set_always_fatal(fatal_mask);

        args = g_strdupv(argv);

        context = g_option_context_new("");
        g_option_context_add_main_entries(context, entries, NULL);
        if (!g_option_context_parse_strv(context, &args, &error)) {
                g_printerr("option parsing failed: %s\n", error->message);
                return 1;
        }

        if (opt_version) {
                g_printf("Version %.1f\n", VERSION);
                return 0;
        }

        if (config_file && !g_file_test(config_file, G_FILE_TEST_EXISTS)) {
                g_printerr("No such configuration file: %s\n", config_file);
                return 3;
        }

        run_once = opt_run_once;

        config = load_config_file(config_file, &error);
        if (!config) {
                g_printerr("Loading config file failed: %s\n", error->message);
                return 4;
        }

        log_level = (opt_debug) ? G_LOG_LEVEL_MASK : config->log_level;

        setup_logging(PROGRAM, log_level, opt_output_systemd);
        hawkbit_init(config, on_new_software_ready_cb);

        return hawkbit_start_service_sync();
}
