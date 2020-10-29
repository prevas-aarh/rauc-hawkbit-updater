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

#ifndef __HAWKBIT_CLIENT_H__
#define __HAWKBIT_CLIENT_H__

#include <glib.h>
#include <glib/gtypes.h>
#include <stdio.h>

#include "config-file.h"

#define RHU_HAWKBIT_CLIENT_ERROR rhu_hawkbit_client_error_quark()
GQuark rhu_hawkbit_client_error_quark(void);

typedef enum {
        RHU_HAWKBIT_CLIENT_ERROR_ALREADY_IN_PROGRESS,
        RHU_HAWKBIT_CLIENT_ERROR_JSON_RESPONSE_PARSE,
        RHU_HAWKBIT_CLIENT_ERROR_DOWNLOAD,
} RHUHawkbitClientError;

// uses CURLcode as error codes
#define RHU_HAWKBIT_CLIENT_CURL_ERROR rhu_hawkbit_client_curl_error_quark()
GQuark rhu_hawkbit_client_curl_error_quark(void);

// uses HTTP codes as error codes
#define RHU_HAWKBIT_CLIENT_HTTP_ERROR rhu_hawkbit_client_http_error_quark()
GQuark rhu_hawkbit_client_http_error_quark(void);

#define HAWKBIT_USERAGENT                 "rauc-hawkbit-c-agent/1.0"
#define DEFAULT_CURL_REQUEST_BUFFER_SIZE  512
#define DEFAULT_CURL_DOWNLOAD_BUFFER_SIZE 64 * 1024 // 64KB

extern gboolean run_once;                  /**< only run software check once and exit */

/**
 * @brief HTTP methods.
 */
enum HTTPMethod {
        GET,
        HEAD,
        PUT,
        POST,
        PATCH,
        DELETE
};

enum ActionState {
        ACTION_STATE_NONE,
        ACTION_STATE_ERROR,
        ACTION_STATE_SUCCESS,
        ACTION_STATE_PROCESSING,
        ACTION_STATE_DOWNLOADING,
        ACTION_STATE_INSTALLING,
};

/**
 * @brief struct that contains the context of an HawkBit action.
 */
struct HawkbitAction {
        gchar *id;                    /**< HawkBit action id */
        GMutex mutex;                 /**< mutex used for accessing all other members */
        enum ActionState state;       /**< state of this action */
};

/**
 * @brief struct containing the payload and size of REST body.
 */
typedef struct RestPayload_ {
        gchar *payload;               /**< string representation of payload */
        size_t size;                  /**< size of payload */
} RestPayload;

/**
 * @brief struct containing Curl write callback context.
 */
typedef struct BinaryPayload_ {
        FILE *fp;                     /**< filepointer to download file */
        gint64 filesize;              /**< expected file size of download file */
        gint64 written;               /**< number of bytes written to download file */
        GChecksum *checksum;          /**< checksum of download file */
} BinaryPayload;

/**
 * @brief struct containing
 */
typedef struct Artifact_ {
        gchar *name;                  /**< name of software */
        gchar *version;               /**< software version */
        gint64 size;                  /**< size of software bundle file */
        gchar *download_url;          /**< download URL of software bundle file */
        gchar *feedback_url;          /**< URL status feedback should be sent to */
        gchar *sha1;                  /**< sha1 checksum of software bundle file */
} Artifact;

/**
 * @brief struct containing the new downloaded file.
 */
struct on_new_software_userdata {
        GSourceFunc install_progress_callback;  /**< callback function to be called when new progress */
        GSourceFunc install_complete_callback;  /**< callback function to be called when installation is complete */
        gchar *file;                            /**< downloaded new software file */
        gboolean install_success;               /**< whether the installation succeeded or not (only meaningful for run_once mode!) */
};

/**
 * @brief struct containing the result of the installation.
 */
struct on_install_complete_userdata {
        gboolean install_success;               /**< status of installation */
};

/**
 * @brief Pass config, callback for installation ready and initialize libcurl.
 *        Intended to be called from program's main().
 *
 * @param[in] config Config* to make global
 * @param[in] on_install_ready GSourceFunc to call after artifact download, to
 *                             trigger RAUC installation
 */
void hawkbit_init(Config *config, GSourceFunc on_install_ready);

/**
 * @brief Sets up timeout and event sources, initializes and runs main loop.
 *
 * @return numeric return code, to be returned by main()
 */
int hawkbit_start_service_sync();

/**
 * @brief Callback for install thread, sends msg as progress feedback to
 *        hawkBit.
 *
 * @param[in] msg Progress message
 * @return G_SOURCE_REMOVE is always returned
 */
gboolean hawkbit_progress(const gchar *msg);

/**
 * @brief Callback for install thread, sends installation feedback to hawkBit.
 *
 * @param[in] ptr on_install_complete_userdata* containing set install_success
 * @return G_SOURCE_REMOVE is always returned
 */
gboolean install_complete_cb(gpointer ptr);

/**
 * @brief Frees the memory allocated by a RestPayload
 *
 * @param[in] payload RestPayload to free
 */
void rest_payload_free(RestPayload *payload);

/**
 * @brief Frees the memory allocated by a BinaryPayload
 *
 * @param[in] payload BinaryPayload to free
 */
void binary_payload_free(BinaryPayload *payload);

/**
 * @brief Frees the memory allocated by an Artifact
 *
 * @param[in] artifact Artifact to free
 */
void artifact_free(Artifact *artifact);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(RestPayload, rest_payload_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(BinaryPayload, binary_payload_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(Artifact, artifact_free)

#endif // __HAWKBIT_CLIENT_H__
