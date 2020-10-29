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
 * @file hawkbit-client.c
 * @author Lasse Mikkelsen <lkmi@prevas.dk>
 * @date 19 Sep 2018
 * @brief Hawkbit client
 *
 * Implementation of the hawkBit DDI API.
 *
 * @see https://github.com/rauc/rauc-hawkbit
 * @see https://www.eclipse.org/hawkbit/apis/ddi_api/
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE
#endif
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/statvfs.h>
#include <curl/curl.h>
#include <glib.h>
#include <glib-object.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <libgen.h>
#include <bits/types/struct_tm.h>
#include <gio/gio.h>
#include <sys/reboot.h>

#include "json-helper.h"
#ifdef WITH_SYSTEMD
#include "sd-helper.h"
#endif

#include "hawkbit-client.h"

G_DEFINE_AUTOPTR_CLEANUP_FUNC(CURL, curl_easy_cleanup)

gboolean run_once = FALSE;

/**
 * @brief String representation of HTTP methods.
 */
static const char *HTTPMethod_STRING[] = {
        "GET", "HEAD", "PUT", "POST", "PATCH", "DELETE"
};

static Config *hawkbit_config = NULL;
static GSourceFunc software_ready_cb;
static struct HawkbitAction *active_action = NULL;
static GThread *thread_download = NULL;

GQuark rhu_hawkbit_client_error_quark(void)
{
        return g_quark_from_static_string("rhu_hawkbit_client_error_quark");
}

GQuark rhu_hawkbit_client_curl_error_quark(void)
{
        return g_quark_from_static_string("rhu_hawkbit_client_curl_error_quark");
}

GQuark rhu_hawkbit_client_http_error_quark(void)
{
        return g_quark_from_static_string("rhu_hawkbit_client_http_error_quark");
}

/**
 * @brief Create and initialize an HawkbitAction.
 *
 * @return Pointer to initialized HawkbitAction..
 */
static struct HawkbitAction *action_new(void)
{
        struct HawkbitAction *action = g_new0(struct HawkbitAction, 1);

        g_mutex_init(&action->mutex);
        g_cond_init(&action->cond);
        action->id = NULL;
        action->state = ACTION_STATE_NONE;

        return action;
}

/**
 * @brief Get available free space of a mounted file system.
 *
 * @param[in]  path       Absolute Path of a disk device node containing a mounted file system
 * @param[out] free_space Pointer to goffset containing the free space in bytes
 * @return TRUE if free space calculation succeeded, FALSE otherwise
 */
static gboolean get_available_space(const char *path, goffset *free_space, GError **error)
{
        struct statvfs stat;
        g_autofree gchar *npath = g_strdup(path);
        const char *rpath = dirname(npath);

        g_return_val_if_fail(path, FALSE);
        g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

        if (statvfs(rpath, &stat)) {
                int err = errno;
                g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                            "Failed to calculate free space for %s: %s", path, g_strerror(err));
                return FALSE;
        }

        // the available free space is f_bsize * f_bavail
        *free_space = (goffset) stat.f_bsize * (goffset) stat.f_bavail;
        return TRUE;
}

/**
 * @brief Curl callback writing RAUC bundle file data to BinaryPayload*->fp (expected opened
 * writable) tracking written data and calculating hawkbit checksum.
 *
 * @see   https://curl.haxx.se/libcurl/c/CURLOPT_WRITEFUNCTION.html
 */
static size_t curl_write_to_file_cb(const void *content, size_t size, size_t nmemb, void *data)
{
        BinaryPayload *p = NULL;
        size_t written;

        g_return_val_if_fail(content, 0);
        g_return_val_if_fail(data, 0);

        p = (BinaryPayload *) data;

        written = fwrite(content, size, nmemb, p->fp);
        p->written += written;

        if (p->checksum)
                g_checksum_update(p->checksum, content, written);

        return written;
}

/**
 * @brief Add string to Curl headers, avoiding overwriting an existing
 *        non-empty list on failure.
 *
 * @param[out] headers curl_slist** of already set headers
 * @param[in]  string  Header to add
 * @param[out] error   Error
 * @return TRUE if string was added to headers successfully, FALSE otherwise (error set)
 */
static gboolean add_curl_header(struct curl_slist **headers, const char *string, GError **error)
{
        struct curl_slist *temp = NULL;

        g_return_val_if_fail(headers, FALSE);
        g_return_val_if_fail(string, FALSE);
        g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

        temp = curl_slist_append(*headers, string);
        if (!temp) {
                curl_slist_free_all(*headers);
                g_set_error(error, RHU_HAWKBIT_CLIENT_CURL_ERROR, CURLE_FAILED_INIT,
                            "Could not add header %s", string);
                return FALSE;
        }

        *headers = temp;
        return TRUE;
}

/**
 * @brief Add hawkBit authorization header to Curl headers.
 *
 * @param[out] headers curl_slist** of already set headers
 * @param[out] error   Error
 * @return TRUE if authorization method set in config and header was added successfully, TRUE if no
 *         authorization method set, FALSE otherwise (error set)
 */
static gboolean set_auth_curl_header(struct curl_slist **headers, GError **error)
{
        gboolean res = TRUE;
        g_autofree gchar *token = NULL;

        g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

        if (hawkbit_config->auth_token)
                token = g_strdup_printf("Authorization: TargetToken %s",
                                        hawkbit_config->auth_token);
        else if (hawkbit_config->gateway_token)
                token = g_strdup_printf("Authorization: GatewayToken %s",
                                        hawkbit_config->gateway_token);
        if (token)
                res = add_curl_header(headers, token, error);

        return res;
}

/**
 * @brief Set common Curl options, namely user agent, connect timeout, SSL
 *        verify peer and SSL verify host options.
 *
 * @param[in] curl Curl handle
 */
static void set_default_curl_opts(CURL *curl)
{
        g_return_if_fail(curl);

        curl_easy_setopt(curl, CURLOPT_USERAGENT, HAWKBIT_USERAGENT);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, hawkbit_config->connect_timeout);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, hawkbit_config->ssl_verify ? 1L : 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, hawkbit_config->ssl_verify ? 1L : 0L);
}

/**
 * @brief Download download_url to file.
 *
 * @param[in]  download_url URL to download from
 * @param[in]  file         Download destination
 * @param[in]  filesize     Expected file size in bytes
 * @param[out] sha1sum      Calculated checksum or NULL
 * @param[out] speed        Average download speed
 * @param[out] error        Error
 * @return TRUE if download succeeded, FALSE otherwise (error set)
 */
static gboolean get_binary(const gchar *download_url, const gchar *file, gint64 filesize,
                           gchar **sha1sum, curl_off_t *speed, GError **error)
{
        g_autoptr(CURL) curl = NULL;
        g_autoptr(BinaryPayload) payload = NULL;
        CURLcode curl_code;
        glong http_code = 0;
        struct curl_slist *headers = NULL;
        g_autofree gchar *token = NULL;

        g_return_val_if_fail(download_url, FALSE);
        g_return_val_if_fail(file, FALSE);
        g_return_val_if_fail(sha1sum == NULL || *sha1sum == NULL, FALSE);
        g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

        payload = g_new0(BinaryPayload, 1);
        payload->filesize = filesize;
        payload->fp = g_fopen(file, "wb");
        if (!payload->fp) {
                int err = errno;
                g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                            "Failed to open %s for download: %s", file, g_strerror(err));
                return FALSE;
        }

        curl = curl_easy_init();
        if (!curl) {
                g_set_error(error, RHU_HAWKBIT_CLIENT_CURL_ERROR, CURLE_FAILED_INIT,
                            "Unable to start libcurl easy session");
                return FALSE;
        }

        if (sha1sum)
                payload->checksum = g_checksum_new(G_CHECKSUM_SHA1);

        set_default_curl_opts(curl);
        curl_easy_setopt(curl, CURLOPT_URL, download_url);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 8L);
        curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, DEFAULT_CURL_DOWNLOAD_BUFFER_SIZE);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_file_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, payload);

        // abort if slower than 100 bytes/sec during 60 seconds
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 100L);

        if (!set_auth_curl_header(&headers, error))
                return FALSE;

        // set up request headers
        if (!add_curl_header(&headers, "Accept: application/octet-stream", error))
                return FALSE;

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // perform transfer
        curl_code = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        curl_easy_getinfo(curl, CURLINFO_SPEED_DOWNLOAD_T, speed);
        curl_slist_free_all(headers);

        if (curl_code != CURLE_OK) {
                g_set_error(error, RHU_HAWKBIT_CLIENT_CURL_ERROR, curl_code, "%s",
                            curl_easy_strerror(curl_code));
                return FALSE;
        }
        if (http_code != 200) {
                g_set_error(error, RHU_HAWKBIT_CLIENT_HTTP_ERROR, http_code,
                            "HTTP request failed: %ld", http_code);
                return FALSE;
        }

        // if checksum enabled then return the value
        if (payload->checksum)
                *sha1sum = g_strdup(g_checksum_get_string(payload->checksum));

        return TRUE;
}

/**
 * @brief Curl callback writing REST response to RestPayload*->payload buffer.
 *
 * @see   https://curl.haxx.se/libcurl/c/CURLOPT_WRITEFUNCTION.html
 */
static size_t curl_write_cb(const void *content, size_t size, size_t nmemb, void *data)
{
        RestPayload *p = NULL;
        size_t real_size = size * nmemb;

        g_return_val_if_fail(content, 0);
        g_return_val_if_fail(data, 0);

        p = (RestPayload *) data;
        p->payload = (gchar *) g_realloc(p->payload, p->size + real_size + 1);

        // copy content to buffer
        memcpy(&(p->payload[p->size]), content, real_size);
        p->size += real_size;
        p->payload[p->size] = '\0';

        return real_size;
}

/**
 * @brief Perform REST request with JSON data, expecting response JSON data.
 *
 * @param[in]  method             HTTP Method, e.g. GET
 * @param[in]  url                URL used in HTTP REST request
 * @param[in]  jsonRequestBody    REST request body. If NULL, no body is sent
 * @param[out] jsonResponseParser Return location for a REST response or NULL to skip response
 *                                parsing
 * @param[out] error              Error
 * @return TRUE if request and response parser (if given) suceeded, FALSE otherwise (error set).
 */
static gboolean rest_request(enum HTTPMethod method, const gchar *url,
                             JsonBuilder *jsonRequestBody, JsonParser **jsonResponseParser,
                             GError **error)
{
        g_autofree gchar *postdata = NULL, *token = NULL;
        g_autoptr(RestPayload) fetch_buffer = NULL;
        struct curl_slist *headers = NULL;
        g_autoptr(CURL) curl = NULL;
        glong http_code = 0;
        CURLcode res;

        g_return_val_if_fail(url, FALSE);
        g_return_val_if_fail(jsonResponseParser == NULL || *jsonResponseParser == NULL, FALSE);
        g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

        curl = curl_easy_init();
        if (!curl) {
                g_set_error(error, RHU_HAWKBIT_CLIENT_CURL_ERROR, CURLE_FAILED_INIT,
                            "Unable to start libcurl easy session");
                return FALSE;
        }

        // init response buffer
        fetch_buffer = g_new0(RestPayload, 1);
        fetch_buffer->size = 0;
        fetch_buffer->payload = g_malloc0(DEFAULT_CURL_REQUEST_BUFFER_SIZE);

        // set up CURL options
        set_default_curl_opts(curl);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, HTTPMethod_STRING[method]);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, hawkbit_config->timeout);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fetch_buffer);

        if (jsonRequestBody) {
                g_autoptr(JsonGenerator) generator = json_generator_new();
                g_autoptr(JsonNode) req_root = json_builder_get_root(jsonRequestBody);
                g_autofree gchar *json_req_str = NULL;

                json_generator_set_root(generator, req_root);
                postdata = json_generator_to_data(generator, NULL);
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postdata);
                json_req_str = json_to_string(req_root, TRUE);
                g_debug("Request body: %s", json_req_str);
        }

        // set up request headers
        if (!add_curl_header(&headers, "Accept: application/json;charset=UTF-8", error))
                return FALSE;

        if (!set_auth_curl_header(&headers, error))
                return FALSE;

        if (jsonRequestBody &&
            !add_curl_header(&headers, "Content-Type: application/json;charset=UTF-8", error))
                return FALSE;

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // perform request
        res = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        curl_slist_free_all(headers);
        if (res != CURLE_OK) {
                g_set_error(error, RHU_HAWKBIT_CLIENT_CURL_ERROR, res, "%s",
                            curl_easy_strerror(res));
                return FALSE;
        }
        if (http_code != 200) {
                g_set_error(error, RHU_HAWKBIT_CLIENT_HTTP_ERROR, http_code,
                            "HTTP request failed: %ld; server response: %s", http_code,
                            fetch_buffer->payload);
                return FALSE;
        }

        if (jsonResponseParser && fetch_buffer->size > 0) {
                // process JSON repsonse
                g_autoptr(JsonParser) parser = json_parser_new_immutable();
                JsonNode *resp_root = NULL;
                g_autofree gchar *json_resp_str = NULL;

                if (!json_parser_load_from_data(parser, fetch_buffer->payload, fetch_buffer->size,
                                                error))
                        return FALSE;

                resp_root = json_parser_get_root(parser);
                json_resp_str = json_to_string(resp_root, TRUE);
                g_debug("Response body: %s", json_resp_str);
                *jsonResponseParser = g_steal_pointer(&parser);
        }

        return TRUE;
}

/**
 * @brief Build hawkBit JSON request.
 *
 * @see https://www.eclipse.org/hawkbit/rest-api/rootcontroller-api-guide/#_post_tenant_controller_v1_controllerid_deploymentbase_actionid_feedback
 *
 * @param[in] id         hawkBit action ID or NULL (configData usecase)
 * @param[in] detail     Detail message or NULL (configData usecase)
 * @param[in] finished   hawkBit status of the result
 * @param[in] execution  hawkBit status of the action execution
 * @param[in] attributes hawkBit controller attributes or NULL (feedback usecase)
 * @return JsonBuilder* with built hawkBit request
 */
static JsonBuilder* json_build_status(const gchar *id, const gchar *detail, const gchar *finished,
                                      const gchar *execution, GHashTable *attributes)
{
        GHashTableIter iter;
        gpointer key, value;
        g_autoptr(JsonBuilder) builder = NULL;
        time_t current_time;
        struct tm time_info;
        char timeString[16];

        g_return_val_if_fail(finished, NULL);
        g_return_val_if_fail(execution, NULL);

        // get current time in UTC
        time(&current_time);
        gmtime_r(&current_time, &time_info);
        strftime(timeString, sizeof(timeString), "%Y%m%dT%H%M%S", &time_info);

        builder = json_builder_new();

        // build json status
        json_builder_begin_object(builder);

        if (id) {
                json_builder_set_member_name(builder, "id");
                json_builder_add_string_value(builder, id);
        }

        json_builder_set_member_name(builder, "time");
        json_builder_add_string_value(builder, timeString);

        json_builder_set_member_name(builder, "status");
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "result");
        json_builder_begin_object(builder);

        json_builder_set_member_name(builder, "finished");
        json_builder_add_string_value(builder, finished);
        json_builder_end_object(builder);

        json_builder_set_member_name(builder, "execution");
        json_builder_add_string_value(builder, execution);

        if (detail) {
                json_builder_set_member_name(builder, "details");
                json_builder_begin_array(builder);
                json_builder_add_string_value(builder, detail);
                json_builder_end_array(builder);
        }
        json_builder_end_object(builder);

        if (attributes) {
                json_builder_set_member_name(builder, "data");
                json_builder_begin_object(builder);
                g_hash_table_iter_init(&iter, attributes);
                while (g_hash_table_iter_next(&iter, &key, &value)) {
                        json_builder_set_member_name(builder, key);
                        json_builder_add_string_value(builder, value);
                }
                json_builder_end_object(builder);
        }
        json_builder_end_object(builder);

        return g_steal_pointer(&builder);
}

/**
 * @brief Send feedback to hawkBit.
 *
 * @param[in]  url        hawkBit URL used for request
 * @param[in]  id         hawkBit action ID
 * @param[in]  detail     Detail message
 * @param[in]  finished   hawkBit status of the result
 * @param[in]  execution  hawkBit status of the action execution
 * @param[out] error      Error
 * @return TRUE if feedback was sent successfully, FALSE otherwise (error set)
 */
static gboolean feedback(const gchar *url, const gchar *id, const gchar *detail,
                         const gchar *finished, const gchar *execution, GError **error)
{
        g_autoptr(JsonBuilder) builder = NULL;
        gboolean res = FALSE;

        g_return_val_if_fail(url, FALSE);
        g_return_val_if_fail(id, FALSE);
        g_return_val_if_fail(detail, FALSE);
        g_return_val_if_fail(finished, FALSE);
        g_return_val_if_fail(execution, FALSE);
        g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

        if (!g_strcmp0(finished, "failure"))
                g_warning("%s", detail);
        else
                g_message("%s", detail);

        builder = json_build_status(id, detail, finished, execution, NULL);

        res = rest_request(POST, url, builder, NULL, error);
        if (!res)
                g_prefix_error(error, "Failed to report \"%s\" feedback: ", detail);

        return res;
}

/**
 * @brief Send progress feedback to hawkBit (finished=none, execution=proceeding).
 *
 * @param[in]  url    hawkBit URL used for request
 * @param[in]  id     hawkBit action ID
 * @param[in]  detail Detail message
 * @param[out] error  Error
 * @return TRUE if feedback was sent successfully, FALSE otherwise (error set)
 */
static gboolean feedback_progress(const gchar *url, const gchar *id, const gchar *detail,
                                  GError **error)
{
        gboolean res = FALSE;

        g_return_val_if_fail(url, FALSE);
        g_return_val_if_fail(id, FALSE);
        g_return_val_if_fail(detail, FALSE);
        g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

        res = feedback(url, id, detail, "none", "proceeding", error);
        g_prefix_error(error, "Progress feedback: ");

        return res;
}

/**
 * @brief Get polling sleep time from hawkBit JSON response.
 *
 * @param[in] root JsonNode* with hawkBit response
 * @return time to sleep in seconds, either from JSON or (if not found) from config's retry_wait
 */
static long json_get_sleeptime(JsonNode *root)
{
        g_autofree gchar *sleeptime_str = NULL;
        g_autoptr(GError) error = NULL;
        struct tm time;

        g_return_val_if_fail(root, 0L);

        sleeptime_str = json_get_string(root, "$.config.polling.sleep", &error);
        if (!sleeptime_str) {
                g_warning("Polling sleep time not found: %s. Using fallback: %ds",
                          error->message, hawkbit_config->retry_wait);
                return hawkbit_config->retry_wait;
        }

        strptime(sleeptime_str, "%T", &time);
        return (time.tm_sec + (time.tm_min * 60) + (time.tm_hour * 60 * 60));
}

/**
 * @brief Build API URL
 *
 * @param path[in] a printf()-like format string describing the API path or NULL for base path
 * @param ... The arguments to be inserted in path
 *
 * @return a newly allocated full API URL
 */
__attribute__((__format__(__printf__, 1, 2)))
static gchar* build_api_url(const gchar *path, ...)
{
        g_autofree gchar *buffer = NULL;
        va_list args;

        va_start(args, path);
        buffer = g_strdup_vprintf(path, args);
        va_end(args);

        return g_strdup_printf(
                "%s://%s/%s/controller/v1/%s%s%s",
                hawkbit_config->ssl ? "https" : "http",
                hawkbit_config->hawkbit_server, hawkbit_config->tenant_id,
                hawkbit_config->controller_id,
                buffer ? "/" : "",
                buffer ? buffer : "");
}

gboolean hawkbit_progress(const gchar *msg)
{
        g_autofree gchar *feedback_url = NULL;
        g_autoptr(GError) error = NULL;

        g_return_val_if_fail(msg, FALSE);

        g_mutex_lock(&active_action->mutex);

        feedback_url = build_api_url("deploymentBase/%s/feedback", active_action->id);

        if (!feedback_progress(feedback_url, active_action->id, msg, &error))
                g_warning("%s", error->message);

        g_mutex_unlock(&active_action->mutex);

        return G_SOURCE_REMOVE;
}

/**
 * @brief Provide meta information that will allow the hawkBit to identify the device on a hardware
 * level.
 *
 * @see https://www.eclipse.org/hawkbit/rest-api/rootcontroller-api-guide/#_put_tenant_controller_v1_controllerid_configdata
 *
 * @param[out] error Error
 * @return TRUE if identification succeeded, FALSE otherwise (error set)
 */
static gboolean identify(GError **error)
{
        g_autofree gchar *put_config_data_url = NULL;
        g_autoptr(JsonBuilder) builder = NULL;

        g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

        g_debug("Providing meta information to hawkbit server");
        put_config_data_url = build_api_url("configData");

        builder = json_build_status(NULL, NULL, "success", "closed", hawkbit_config->device);

        return rest_request(PUT, put_config_data_url, builder, NULL, error);
}

/**
 * @brief Deletes RAUC bundle at config's bundle_download_location.
 */
static void process_deployment_cleanup()
{
        if (!g_file_test(hawkbit_config->bundle_download_location, G_FILE_TEST_IS_REGULAR))
                return;

        if (g_remove(hawkbit_config->bundle_download_location))
                g_warning("Failed to delete file: %s", hawkbit_config->bundle_download_location);
}

gboolean install_complete_cb(gpointer ptr)
{
        gboolean res = FALSE;
        g_autoptr(GError) error = NULL;
        struct on_install_complete_userdata *result = ptr;
        g_autofree gchar *feedback_url = NULL;

        g_return_val_if_fail(ptr, FALSE);

        g_mutex_lock(&active_action->mutex);

        active_action->state = result->install_success ? ACTION_STATE_SUCCESS : ACTION_STATE_ERROR;
        feedback_url = build_api_url("deploymentBase/%s/feedback", active_action->id);
        res = feedback(
                feedback_url, active_action->id,
                result->install_success ? "Software bundle installed successfully."
                : "Failed to install software bundle.",
                result->install_success ? "success" : "failure",
                "closed", &error);

        if (!res)
                g_warning("%s", error->message);

        process_deployment_cleanup();
        g_mutex_unlock(&active_action->mutex);

        if (result->install_success && hawkbit_config->post_update_reboot) {
                sync();
                if (reboot(RB_AUTOBOOT) < 0)
                        g_critical("Failed to reboot: %s", g_strerror(errno));
        }

        return G_SOURCE_REMOVE;
}

/**
 * @brief Thread to download given Artifact, verfiy its checksum, send hawkBit
 * feedback and call software_ready_cb() callback on success.
 *
 * @param[in] data Artifact* to process
 * @return gpointer being 1 (TRUE) if download succeeded, 0 (FALSE) otherwise. The return value is
 *         meant to be used with the GPOINTER_TO_INT() macro only.
 *         Note that if the download thread waited for installation to finish ('run_once' mode),
 *         TRUE means both installation and download succeeded.
 */
static gpointer download_thread(gpointer data)
{
        struct on_new_software_userdata userdata = {
                .install_progress_callback = (GSourceFunc) hawkbit_progress,
                .install_complete_callback = install_complete_cb,
                .file = hawkbit_config->bundle_download_location,
                .install_success = FALSE,
        };
        g_autoptr(GError) error = NULL, feedback_error = NULL;
        g_autofree gchar *msg = NULL, *sha1sum = NULL;
        g_autoptr(Artifact) artifact = data;
        curl_off_t speed;

        g_return_val_if_fail(data, NULL);

        g_mutex_lock(&active_action->mutex);

        if (active_action->state == ACTION_STATE_CANCEL_REQUESTED)
                goto cancel;

        active_action->state = ACTION_STATE_DOWNLOADING;
        g_mutex_unlock(&active_action->mutex);

        g_message("Start downloading: %s", artifact->download_url);

        // Download software bundle (artifact)
        if (!get_binary(artifact->download_url, hawkbit_config->bundle_download_location,
                        artifact->size, &sha1sum, &speed, &error)) {
                g_prefix_error(&error, "Download failed: ");
                goto report_err;
        }

        if (active_action->state == ACTION_STATE_CANCEL_REQUESTED)
                goto cancel;

        // notify hawkbit that download is complete
        msg = g_strdup_printf("Download complete. %.2f MB/s",
                              (double)speed/(1024*1024));
        g_mutex_lock(&active_action->mutex);
        if (!feedback_progress(artifact->feedback_url, active_action->id, msg, &error)) {
                g_warning("%s", error->message);
                g_clear_error(&error);
        }
        g_mutex_unlock(&active_action->mutex);

        // validate checksum
        if (g_strcmp0(artifact->sha1, sha1sum)) {
                g_set_error(&error, RHU_HAWKBIT_CLIENT_ERROR, RHU_HAWKBIT_CLIENT_ERROR_DOWNLOAD,
                            "Software: %s V%s. Invalid checksum: %s expected %s", artifact->name,
                            artifact->version, sha1sum, artifact->sha1);
                goto report_err;
        }

        g_mutex_lock(&active_action->mutex);
        if (!feedback_progress(artifact->feedback_url, active_action->id, "File checksum OK.",
                               &error)) {
                g_warning("%s", error->message);
                g_clear_error(&error);
        }
        g_mutex_unlock(&active_action->mutex);
        // last chance to cancel installation
        g_mutex_lock(&active_action->mutex);

        if (active_action->state == ACTION_STATE_CANCEL_REQUESTED)
                goto cancel;

        // start installation, cancelations are impossible now
        active_action->state = ACTION_STATE_INSTALLING;
        g_cond_signal(&active_action->cond);
        g_mutex_unlock(&active_action->mutex);

        software_ready_cb(&userdata);

        return GINT_TO_POINTER(userdata.install_success);

report_err:
        g_mutex_trylock(&active_action->mutex);
        if (!feedback(artifact->feedback_url, active_action->id, error->message, "failure",
                      "closed", &feedback_error))
                g_warning("%s", feedback_error->message);

        active_action->state = ACTION_STATE_ERROR;

cancel:
        g_mutex_trylock(&active_action->mutex);
        if (active_action->state == ACTION_STATE_CANCEL_REQUESTED)
                active_action->state = ACTION_STATE_CANCELED;

        process_deployment_cleanup();

        g_cond_signal(&active_action->cond);
        g_mutex_unlock(&active_action->mutex);

        return GINT_TO_POINTER(FALSE);
}

/**
 * @brief Process hawkBit deployment described by req_root.
 *        Must be called under locked active_action->mutex.
 *
 * @param[in]  req_root JsonNode* describing the deployment to process
 * @param[out] error    Error
 * @return TRUE if processing deployment succeeded, FALSE otherwise (error set)
 */
static gboolean process_deployment(JsonNode *req_root, GError **error)
{
        g_autoptr(Artifact) artifact = NULL;
        g_autofree gchar *deployment = NULL, *feedback_url = NULL;
        g_autoptr(JsonParser) json_response_parser = NULL;
        g_autoptr(JsonArray) json_chunks = NULL, json_artifacts = NULL;
        JsonNode *resp_root = NULL, *json_chunk = NULL, *json_artifact = NULL;
        goffset freespace;

        g_return_val_if_fail(req_root, FALSE);
        g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
        g_assert_false(g_mutex_trylock(&active_action->mutex));

        if (active_action->state >= ACTION_STATE_PROCESSING) {
                g_set_error(error, RHU_HAWKBIT_CLIENT_ERROR,
                            RHU_HAWKBIT_CLIENT_ERROR_ALREADY_IN_PROGRESS,
                            "Deployment %s is already in progress.", active_action->id);
                // no need to tell hawkBit about this
                return FALSE;
        }

        active_action->state = ACTION_STATE_PROCESSING;

        // get deployment URL
        deployment = json_get_string(req_root, "$._links.deploymentBase.href", error);
        if (!deployment)
                goto error;

        // get deployment resource
        if (!rest_request(GET, deployment, NULL, &json_response_parser, error))
                goto error;

        resp_root = json_parser_get_root(json_response_parser);

        // remember deployment's action id
        g_free(active_action->id);
        active_action->id = json_get_string(resp_root, "$.id", error);
        if (!active_action->id)
                goto error;

        feedback_url = build_api_url("deploymentBase/%s/feedback", active_action->id);

        json_chunks = json_get_array(resp_root, "$.deployment.chunks", error);
        if (!json_chunks)
                goto proc_error;

        // downloading multiple chunks not supported, only first chunk is downloaded (RAUC bundle)
        json_chunk = json_array_get_element(json_chunks, 0);
        json_artifacts = json_get_array(json_chunk, "$.artifacts", error);
        if (!json_artifacts)
                goto proc_error;

        json_artifact = json_array_get_element(json_artifacts, 0);

        // get artifact information
        artifact = g_new0(Artifact, 1);
        artifact->version = json_get_string(json_chunk, "$.version", error);
        if (!artifact->version)
                goto proc_error;

        artifact->name = json_get_string(json_chunk, "$.name", error);
        if (!artifact->name)
                goto proc_error;

        artifact->size = json_get_int(json_artifact, "$.size", error);
        if (!artifact->size && error)
                goto proc_error;

        artifact->sha1 = json_get_string(json_artifact, "$.hashes.sha1", error);
        if (!artifact->sha1)
                goto proc_error;

        // favour https download
        artifact->download_url = json_get_string(json_artifact, "$._links.download.href", NULL);
        if (!artifact->download_url)
                artifact->download_url = json_get_string(
                        json_artifact, "$._links.download-http.href", error);

        if (!artifact->download_url) {
                g_prefix_error(error, "\"$._links.download{-http,}.href\": ");
                goto proc_error;
        }

        g_message("New software ready for download (Name: %s, Version: %s, Size: %" G_GINT64_FORMAT " bytes, URL: %s)",
                  artifact->name, artifact->version, artifact->size, artifact->download_url);

        // check if there is enough free diskspace
        if (!get_available_space(hawkbit_config->bundle_download_location, &freespace, error))
                goto proc_error;

        if (freespace < artifact->size) {
                // notify hawkbit that there is not enough free space
                g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_NOSPC,
                            "File size %" G_GINT64_FORMAT " exceeds available space %" G_GOFFSET_FORMAT,
                            artifact->size, freespace);
                goto proc_error;
        }

        artifact->feedback_url = g_steal_pointer(&feedback_url);

        // unref/free previous download thread by joining it
        if (thread_download)
                g_thread_join(thread_download);

        // start download thread
        thread_download = g_thread_new("downloader", download_thread,
                                       (gpointer) g_steal_pointer(&artifact));

        return TRUE;

proc_error:
        feedback(feedback_url, active_action->id, (*error)->message, "failure", "closed", NULL);

error:
        // clean up failed deployment
        process_deployment_cleanup();
        active_action->state = ACTION_STATE_NONE;

        return FALSE;
}

/**
 * @brief Process hawkBit cancel action described by req_root.
 *
 * @param[in]  req_root JsonNode* describing the cancel action
 * @param[out] error    Error
 * @return TRUE if cancel action succeeded, FALSE otherwise (error set)
 */
static gboolean process_cancel(JsonNode *req_root, GError **error)
{
        gboolean res = TRUE;
        g_autofree gchar *cancel_url = NULL, *feedback_url = NULL, *stop_id = NULL, *msg = NULL;
        g_autoptr(JsonParser) json_response_parser = NULL;
        JsonNode *resp_root = NULL;

        g_return_val_if_fail(req_root, FALSE);
        g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

        // get cancel url
        cancel_url = json_get_string(req_root, "$._links.cancelAction.href", error);
        if (!cancel_url)
                return FALSE;

        // retrieve cancel details
        if (!rest_request(GET, cancel_url, NULL, &json_response_parser, error))
                return FALSE;

        resp_root = json_parser_get_root(json_response_parser);

        // retrieve stop id
        stop_id = json_get_string(resp_root, "$.cancelAction.stopId", error);
        if (!stop_id)
                return FALSE;

        g_message("Received cancelation for action %s", stop_id);

        // send cancel feedback
        feedback_url = build_api_url("cancelAction/%s/feedback", stop_id);

        // cancel action if install not started yet
        g_mutex_lock(&active_action->mutex);
        if (!g_strcmp0(stop_id, active_action->id) &&
            (active_action->state == ACTION_STATE_PROCESSING ||
             active_action->state == ACTION_STATE_DOWNLOADING)) {

                g_debug("Action %s is in state %d, waiting for cancel request to be processed",
                        stop_id, active_action->state);
                active_action->state = ACTION_STATE_CANCEL_REQUESTED;

                while (active_action->state == ACTION_STATE_CANCEL_REQUESTED)
                        g_cond_wait(&active_action->cond, &active_action->mutex);
        }
        if (g_strcmp0(stop_id, active_action->id))
                active_action->state = ACTION_STATE_NONE;

        // send feedback
        switch (active_action->state) {
        case ACTION_STATE_NONE:
                // action unknown, acknowledge cancelation nonetheless
                g_debug("Received cancelation for unprocessed action %s, acknowledging.",
                        stop_id);
        // fall through
        case ACTION_STATE_CANCELED:
                res = feedback(feedback_url, stop_id, "Action canceled.", "success", "closed",
                               error);
                break;
        case ACTION_STATE_SUCCESS:
                g_debug("Cancelation impossible, installation succeeded already");
                break;
        case ACTION_STATE_ERROR:
                g_debug("Cancelation impossible, installation failed already");
                break;
        case ACTION_STATE_INSTALLING:
                msg = g_strdup("Cancelation impossible, installation started already.");
                res = feedback(feedback_url, stop_id, msg, "success", "rejected", error);
                if (res) {
                        res = FALSE;
                        g_set_error(error, RHU_HAWKBIT_CLIENT_ERROR,
                                    RHU_HAWKBIT_CLIENT_ERROR_CANCELATION, "%s", msg);
                }
                break;
        default:
                // other states are not expected here
                g_critical("Unexpected action state after cancel request: %d", active_action->state);
                g_assert_not_reached();
                break;
        }

        g_mutex_unlock(&active_action->mutex);
        return res;
}

void hawkbit_init(Config *config, GSourceFunc on_install_ready)
{
        g_return_if_fail(config);

        hawkbit_config = config;
        software_ready_cb = on_install_ready;
        curl_global_init(CURL_GLOBAL_ALL);
}

typedef struct ClientData_ {
        GMainLoop *loop;
        gboolean res;
        long hawkbit_interval_check_sec;
        long last_run_sec;
} ClientData;

/**
 * @brief Callback for main loop, should run regularly, polls controller base poll resource and
 * triggers appropriate actions.
 *
 * @param[in] user_data ClientData*
 * @return TRUE if polling controller base resource and running appropriate actions succeeded,
 *         FALSE otherwise
 */
static gboolean hawkbit_pull_cb(gpointer user_data)
{
        ClientData *data = user_data;
        gboolean res = FALSE;
        g_autoptr(GError) error = NULL;
        g_autofree gchar *get_tasks_url = NULL;
        g_autoptr(JsonParser) json_response_parser = NULL;
        JsonNode *json_root = NULL;

        g_return_val_if_fail(user_data, FALSE);

        if (++data->last_run_sec < data->hawkbit_interval_check_sec)
                return G_SOURCE_CONTINUE;

        data->last_run_sec = 0;

        // build hawkBit get tasks URL
        get_tasks_url = build_api_url(NULL);

        g_message("Checking for new software...");
        res = rest_request(GET, get_tasks_url, NULL, &json_response_parser, &error);
        if (!res) {
                if (g_error_matches(error, RHU_HAWKBIT_CLIENT_HTTP_ERROR, 401)) {
                        if (hawkbit_config->auth_token)
                                g_warning("Failed to authenticate. Check if auth_token is correct?");
                        if (hawkbit_config->gateway_token)
                                g_warning("Failed to authenticate. Check if gateway_token is correct?");
                } else {
                        g_warning("Scheduled check for new software failed: %s (%d)",
                                  error->message, error->code);
                }

                data->hawkbit_interval_check_sec = hawkbit_config->retry_wait;
                goto out;
        }

        // owned by the JsonParser and should never be modified or freed
        json_root = json_parser_get_root(json_response_parser);

        // get hawkbit sleep time (how often should we check for new software)
        data->hawkbit_interval_check_sec = json_get_sleeptime(json_root);

        if (json_contains(json_root, "$._links.configData")) {
                // hawkBit has asked us to identify ourselves
                res = identify(&error);
                if (!res) {
                        g_warning("%s", error->message);
                        g_clear_error(&error);
                }
        }
        if (json_contains(json_root, "$._links.deploymentBase")) {
                // hawkBit has a new deployment for us
                g_mutex_lock(&active_action->mutex);
                res = process_deployment(json_root, &error);
                g_mutex_unlock(&active_action->mutex);
                if (!res) {
                        if (g_error_matches(error, RHU_HAWKBIT_CLIENT_ERROR,
                                            RHU_HAWKBIT_CLIENT_ERROR_ALREADY_IN_PROGRESS))
                                g_debug("%s", error->message);
                        else
                                g_warning("%s", error->message);
                }
        } else {
                g_message("No new software.");
        }
        if (json_contains(json_root, "$._links.cancelAction")) {
                res = process_cancel(json_root, &error);
                if (!res) {
                        g_warning("%s", error->message);
                        g_clear_error(&error);
                }
        }

out:
        if (run_once) {
                if (thread_download) {
                        gpointer thread_ret = g_thread_join(thread_download);
                        res = GPOINTER_TO_INT(thread_ret);
                }

                data->res = res;
                g_main_loop_quit(data->loop);
                return G_SOURCE_REMOVE;
        }

        return G_SOURCE_CONTINUE;
}

int hawkbit_start_service_sync()
{
        g_autoptr(GMainContext) ctx = NULL;
        ClientData cdata;
        g_autoptr(GSource) timeout_source = NULL;
        g_autoptr(GSource) event_source = NULL;
        int res = 0;
#ifdef WITH_SYSTEMD
        g_autoptr(sd_event) event = NULL;
#endif

        active_action = action_new();

        ctx = g_main_context_new();
        cdata.loop = g_main_loop_new(ctx, FALSE);
        cdata.hawkbit_interval_check_sec = hawkbit_config->retry_wait;
        cdata.last_run_sec = hawkbit_config->retry_wait;

        // pull every second
        timeout_source = g_timeout_source_new(1000);
        g_source_set_name(timeout_source, "Add timeout");
        g_source_set_callback(timeout_source, (GSourceFunc) hawkbit_pull_cb, &cdata, NULL);
        g_source_attach(timeout_source, ctx);

#ifdef WITH_SYSTEMD
        res = sd_event_default(&event);
        if (res < 0)
                goto finish;
        // enable automatic service watchdog support
        res = sd_event_set_watchdog(event, TRUE);
        if (res < 0)
                goto finish;

        event_source = sd_source_new(event);
        if (!event_source) {
                res = -ENOMEM;
                goto finish;
        }

        // attach systemd source to glib mainloop
        res = sd_source_attach(event_source, cdata.loop);
        if (res < 0)
                goto finish;

        sd_notify(0, "READY=1\nSTATUS=Init completed, start polling HawkBit for new software.");
#endif

        g_main_loop_run(cdata.loop);

        res = cdata.res ? 0 : 1;

#ifdef WITH_SYSTEMD
        sd_notify(0, "STOPPING=1\nSTATUS=Stopped polling HawkBit for new software.");
#endif

#ifdef WITH_SYSTEMD
finish:
        g_source_destroy(event_source);
        sd_event_set_watchdog(event, FALSE);
#endif
        g_main_loop_unref(cdata.loop);
        if (res < 0)
                g_warning("%s", strerror(-res));

        return res;
}

void artifact_free(Artifact *artifact)
{
        if (!artifact)
                return;

        g_free(artifact->name);
        g_free(artifact->version);
        g_free(artifact->download_url);
        g_free(artifact->feedback_url);
        g_free(artifact->sha1);
        g_free(artifact);
}

void rest_payload_free(RestPayload *payload)
{
        if (!payload)
                return;

        g_free(payload->payload);
        g_free(payload);
}

void binary_payload_free(BinaryPayload *payload)
{
        if (!payload)
                return;

        if (payload->fp)
                fclose(payload->fp);
        g_checksum_free(payload->checksum);
        g_free(payload);
}
