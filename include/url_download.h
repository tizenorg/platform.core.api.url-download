/*
 * Copyright (c) 2011 Samsung Electronics Co., Ltd All Rights Reserved
 *
 * Licensed under the Apache License, Version 2.0 (the License);
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. 
 */

#ifndef __TIZEN_WEB_URL_DOWNLOAD_H__
#define __TIZEN_WEB_URL_DOWNLOAD_H__

#include <tizen.h>
#include <app.h>

#ifdef __cplusplus
extern "C"
{
#endif

 /**
 * @addtogroup CAPI_WEB_URL_DOWNLOAD_MODULE
 * @{
 */

/**
 * @brief Name for download service operation. Download Manager application is launched.
 */
#define SERVICE_OPERATION_DOWNLOAD "http://tizen.org/appsvc/operation/download"


/**
 * @brief URL download handle.
 */
typedef struct url_download_s *url_download_h;


/**
 * @brief Enumeration of error code for URL download
 */
typedef enum
{
	URL_DOWNLOAD_ERROR_NONE = TIZEN_ERROR_NONE, /**< Successful */
	URL_DOWNLOAD_ERROR_INVALID_PARAMETER = TIZEN_ERROR_INVALID_PARAMETER, /**< Invalid parameter */
	URL_DOWNLOAD_ERROR_OUT_OF_MEMORY = TIZEN_ERROR_OUT_OF_MEMORY, /**< Out of memory */
	URL_DOWNLOAD_ERROR_IO_ERROR = TIZEN_ERROR_IO_ERROR , /**< Internal I/O error */
	URL_DOWNLOAD_ERROR_NETWORK_UNREACHABLE = TIZEN_ERROR_NETWORK_UNREACHABLE, /**< Network is unreachable */
	URL_DOWNLOAD_ERROR_CONNECTION_TIMED_OUT = TIZEN_ERROR_CONNECTION_TIME_OUT, /**< Connection timed out */
	URL_DOWNLOAD_ERROR_NO_SPACE = TIZEN_ERROR_FILE_NO_SPACE_ON_DEVICE, /**< No space left on device */
	URL_DOWNLOAD_ERROR_FIELD_NOT_FOUND = TIZEN_ERROR_KEY_NOT_AVAILABLE, /**< Specified field not found */
	URL_DOWNLOAD_ERROR_INVALID_STATE = TIZEN_ERROR_WEB_CLASS | 0x21, /**< Invalid state */
	URL_DOWNLOAD_ERROR_CONNECTION_FAILED = TIZEN_ERROR_WEB_CLASS | 0x22, /**< Connection failed */
	URL_DOWNLOAD_ERROR_SSL_FAILED = TIZEN_ERROR_WEB_CLASS | 0x23, /**< SSL negotiation failed */
	URL_DOWNLOAD_ERROR_INVALID_URL = TIZEN_ERROR_WEB_CLASS | 0x24, /**< Invalid URL */
	URL_DOWNLOAD_ERROR_INVALID_DESTINATION = TIZEN_ERROR_WEB_CLASS | 0x25, /**< Invalid destination */
	URL_DOWNLOAD_ERROR_TOO_MANY_DOWNLOADS = TIZEN_ERROR_WEB_CLASS | 0x26, /**< Full of available downloading items */
	URL_DOWNLOAD_ERROR_ALREADY_COMPLETED = TIZEN_ERROR_WEB_CLASS | 0x27, /**< The download is already completed */
} url_download_error_e;


/**
 * @brief Enumerations of state of download
 */
typedef enum
{
	URL_DOWNLOAD_STATE_READY, /**< It is ready to download */
	URL_DOWNLOAD_STATE_DOWNLOADING, /**< The download is currently running */
	URL_DOWNLOAD_STATE_PAUSED, /**< The download is waiting to resume or stop */
	URL_DOWNLOAD_STATE_COMPLETED, /**< The download is completed. */
	URL_DOWNLOAD_STATE_FAILED, /**< The download failed. */
} url_download_state_e;


/**
 * @brief Called when the download is started.
 *
 * @param [in] download The download handle
 * @param [in] content_name The content name to display at UI layer
 * @param [in] mime_type The MIME type string
 * @param [in] user_data The user data passed from url_download_set_started_cb()
 * @pre url_download_start() will cause this callback if you register this callback using url_download_set_started_cb()
 * @see url_download_start()
 * @see url_download_set_started_cb()
 * @see url_download_unset_started_cb()
 */
typedef void (*url_download_started_cb) (url_download_h download,
	const char *content_name, const char *mime_type, void *user_data);


/**
 * @brief Called when the download is paused.
 *
 * @param [in] download The download handle
 * @param [in] user_data The user data passed from url_download_set_paused_cb()
 * @pre url_download_pause() will cause this callback if you register this callback using url_download_set_paused_cb()
 * @see url_download_pause()
 * @see url_download_set_paused_cb()
 * @see url_download_unset_paused_cb()
 */
typedef void (*url_download_paused_cb) (url_download_h download, void *user_data);


/**
 * @brief Called when the download is completed.
 *
 * @param [in] download The download handle
 * @param [in] installed_path The absolute path to the downloaded file
 * @param [in] user_data The user data passed from url_download_set_completed_cb()
 * @pre This callback function will be invoked when the download is completed if you register this callback using url_download_set_paused_cb()
 * @see url_download_set_completed_cb()
 * @see url_download_unset_completed_cb()
 */
typedef void (*url_download_completed_cb) (url_download_h download, const char *installed_path, void *user_data);


/**
 * @brief Called when the download is stopped.
 *
 * @remarks This callback function is invoked when the download is explicitly stopped by url_download_stop(). \n
 * And this callback function is also invoked when the download failed from either downloading or paused state.
 * @param [in] download The download handle
 * @param [in] error The error code \n
 * If the download is stopped with url_download_stop(), the error code is #URL_DOWNLOAD_ERROR_NONE
 * @param [in] user_data The user data passed from url_download_set_stopped_cb()
 * @pre This callback function is invoked if you register this callback using url_download_set_stopped_cb().
 * @see url_download_stop()
 * @see url_download_set_stopped_cb()
 * @see url_download_unset_stopped_cb()
 */
typedef void (*url_download_stopped_cb) (url_download_h download, url_download_error_e error, void *user_data);


/**
 * @brief Called when the progress of download changes.
 *
 * @remarks This callback function is only invoked in the downloading state.
 * @param [in] download The download handle
 * @param [in] received The size of the data received in bytes
 * @param [in] total The total size of the data to receive in bytes
 * @param [in] user_data The user data passed from url_download_set_progress_cb()
 * @pre This callback function is invoked if you register this callback using url_download_set_progress_cb().
 * @see url_download_stop()
 * @see url_download_set_progress_cb()
 * @see url_download_unset_progress_cb()
 */
typedef void (*url_download_progress_cb) (url_download_h download, unsigned long long received, unsigned long long total, void *user_data);


/**
* @brief Called to retrieve the HTTP header field to be included with the download
*
* @remarks The @a field must not be deallocated by an application. 
* @param [in] download The download handle
* @param [in] field The HTTP header field
* @param [in] user_data The user data passed from the foreach function
* @return @c true to continue with the next iteration of the loop, \n @c false to break out of the loop.
* @pre url_download_foreach_http_header_field() will invoke this callback.
* @see url_download_foreach_http_header_field()
*/
typedef bool (*url_download_http_header_field_cb)(url_download_h download, const char *field, void *user_data);


/**
 * @brief Creates a download handle.
 *
 * @remarks The @a download must be released with url_download_destroy() by you.\n
 * The g_type_init() should be called when creating a main loop by user side. \n
 * Because the libsoup, which is http stack library of download module, use gobject internally.
 * @param [out] download A download handle to be newly created on success
 * @return 0 on success, otherwise a negative error value.
 * @retval #URL_DOWNLOAD_ERROR_NONE Successful
 * @retval #URL_DOWNLOAD_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #URL_DOWNLOAD_ERROR_OUT_OF_MEMORY Out of memory
 * @retval #URL_DOWNLOAD_ERROR_IO_ERROR Internal I/O error
 * @post The download state will be #URL_DOWNLOAD_STATE_READY
 * @see url_download_destroy()
 */
int url_download_create(url_download_h *download);


/**
 * @brief Creates a download handle with the given identifier
 *
 * @remarks The @a download must be released with url_download_destroy() by you.\n
 * The g_type_init() should be called when creating a main loop by user side. \n
 * Because the libsoup, which is http stack library of download module, use gobject internally.
 * @param [in] id The identifier for the download unique within the application.
 * @param [out] download A download handle to be newly created on success
 * @return 0 on success, otherwise a negative error value.
 * @retval #URL_DOWNLOAD_ERROR_NONE Successful
 * @retval #URL_DOWNLOAD_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #URL_DOWNLOAD_ERROR_OUT_OF_MEMORY Out of memory
 * @retval #URL_DOWNLOAD_ERROR_IO_ERROR Internal I/O error
 * @post The download state will be #URL_DOWNLOAD_STATE_READY
 * @see url_download_create()
 * @see url_download_destroy()
 */
int url_download_create_by_id(int id, url_download_h *download);


/**
 * @brief Destroys the URL download handle.
 *
 * @param [in] download The download handle
 * @return 0 on success, otherwise a negative error value.
 * @retval #URL_DOWNLOAD_ERROR_NONE Successful
 * @retval #URL_DOWNLOAD_ERROR_INVALID_PARAMETER Invalid parameter
 * @see url_download_create()
 */
int url_download_destroy(url_download_h download);


/**
 * @brief Sets the URL to download.
 *
 * @remarks This function should be called before downloading (See url_download_start())
 * @param [in] download The download handle
 * @param [in] url The URL to download \n
 *  If the @a url is NULL, it clears the previous value.
 * @return 0 on success, otherwise a negative error value.
 * @retval #URL_DOWNLOAD_ERROR_NONE Successful
 * @retval #URL_DOWNLOAD_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #URL_DOWNLOAD_ERROR_OUT_OF_MEMORY Out of memory
 * @retval #URL_DOWNLOAD_ERROR_INVALID_STATE Invalid state
 * @pre The download state must be #URL_DOWNLOAD_STATE_READY or #URL_DOWNLOAD_STATE_COMPLETED.
 * @see url_download_get_url()
 */
int url_download_set_url(url_download_h download, const char *url);


/**
 * @brief Gets the URL to download.
 *
 * @remarks The @a url must be released with free() by you.
 * @param [in] download The download handle
 * @param [out] url The URL to download
 * @return 0 on success, otherwise a negative error value.
 * @retval #URL_DOWNLOAD_ERROR_NONE Successful
 * @retval #URL_DOWNLOAD_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #URL_DOWNLOAD_ERROR_OUT_OF_MEMORY Out of memory
 * @see url_download_set_url()
 */
int url_download_get_url(url_download_h download, char **url);


/**
 * @brief Sets the destination for the downloaded file.
 *
 * @details The file will be downloaded to the specified destination.
 * The downloaded file is saved to an auto-generated file name in the destination. (See url_download_completed_cb())
 * If the destination is not specified, the file will be downloaded to default storage. (See the @ref CAPI_STORAGE_MODULE API)
 *
 * @remarks This function should be called before downloading (see url_download_start())
 * @param [in] download The download handle
 * @param [in] path The absolute path to the downloaded file
 *  If the @a path is NULL, it clears the previous value.
 * @return 0 on success, otherwise a negative error value.
 * @retval #URL_DOWNLOAD_ERROR_NONE Successful
 * @retval #URL_DOWNLOAD_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #URL_DOWNLOAD_ERROR_OUT_OF_MEMORY Out of memory
 * @retval #URL_DOWNLOAD_ERROR_INVALID_STATE Invalid state
 * @pre The download state must be #URL_DOWNLOAD_STATE_READY or #URL_DOWNLOAD_STATE_COMPLETED.
 * @see url_download_get_destination()
 */
int url_download_set_destination(url_download_h download, const char *path);


/**
 * @brief Gets the destination for the downloaded file.
 *
 * @remarks The @a path must be released with free() by you.
 * @param [in] download The download handle
 * @param [out] path The absolute path to the downloaded file
 * @return 0 on success, otherwise a negative error value.
 * @retval #URL_DOWNLOAD_ERROR_NONE Successful
 * @retval #URL_DOWNLOAD_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #URL_DOWNLOAD_ERROR_OUT_OF_MEMORY Out of memory
 * @see url_download_set_destination()
 */
int url_download_get_destination(url_download_h download, char **path);


/**
 * @brief Sets the name for the downloaded file.
 *
 * @details The file will be downloaded to the specified destination as the given file name.
 * If the file name is not specified, the downloaded file is saved to an auto-generated file name in the destination.
 *
 * @remarks This function should be called before downloading (see url_download_start())
 * @param [in] download The download handle
 * @param [in] file_name The file name for the downloaded file
 *  If the @a name is NULL, it clears the previous value.
 * @return 0 on success, otherwise a negative error value.
 * @retval #URL_DOWNLOAD_ERROR_NONE Successful
 * @retval #URL_DOWNLOAD_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #URL_DOWNLOAD_ERROR_OUT_OF_MEMORY Out of memory
 * @retval #URL_DOWNLOAD_ERROR_INVALID_STATE Invalid state
 * @pre The download state must be #URL_DOWNLOAD_STATE_READY or #URL_DOWNLOAD_STATE_COMPLETED.
 * @see url_download_get_file_name()
 */
int url_download_set_file_name(url_download_h download, const char *file_name);


/**
 * @brief Gets the name for the downloaded file.
 *
 * @remarks The @a file_name must be released with free() by you.
 * @param [in] download The download handle
 * @param [out] file_name The file name for the downloaded file
 * @return 0 on success, otherwise a negative error value.
 * @retval #URL_DOWNLOAD_ERROR_NONE Successful
 * @retval #URL_DOWNLOAD_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #URL_DOWNLOAD_ERROR_OUT_OF_MEMORY Out of memory
 * @see url_download_set_file_name()
 */
int url_download_get_file_name(url_download_h download, char **file_name);


/**
 * @brief Sets the service to launch when the notification for the download is selected from the notification tray.
 * @details When the notification for the download is selected from the notification tray, the application which is described by the specified service is launched. \n
 * If you want to launch the current application, use the explicit launch of the @ref CAPI_SERVICE_MODULE API
 * @remarks If the service is not set, the selected notification will be cleared from both the notification tray and the status bar without any action.
 * @param[in] download The download handle
 * @param[in] service The service handle to launch when the notification for the download is selected \n
 *     If the @a service is NULL, it clears the previous value.
 * @return 0 on success, otherwise a negative error value.
 * @retval #URL_DOWNLOAD_ERROR_NONE Successful
 * @retval #URL_DOWNLOAD_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #URL_DOWNLOAD_ERROR_OUT_OF_MEMORY Out of memory
 * @see url_download_get_notification()
 * @see service_create()
 */
int url_download_set_notification(url_download_h download, service_h service);

/**
 * @brief Gets the service to launch when the notification for the download is selected from the notification tray
 * @remarks The @a service must be released with service_destroy() by you.
 * @param[in] download The download handle
 * @param[out] service The service handle to launch when the notification is selected
 * @return 0 on success, otherwise a negative error value.
 * @retval #URL_DOWNLOAD_ERROR_NONE Successful
 * @retval #URL_DOWNLOAD_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #URL_DOWNLOAD_ERROR_OUT_OF_MEMORY Out of memory
 * @see url_download_set_notification()
 */
int url_download_get_notification(url_download_h download, service_h *service);


/**
 * @brief Gets the absolute path to the downloaded file
 *
 * @remarks This function returns #URL_DOWNLOAD_ERROR_INVALID_STATE if the download is not completed. \n
 * The @a path must be released with free() by you.
 * @param [in] download The download handle
 * @param [out] path The absolute path to the downloaded file
 * @return 0 on success, otherwise a negative error value.
 * @retval #URL_DOWNLOAD_ERROR_NONE Successful
 * @retval #URL_DOWNLOAD_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #URL_DOWNLOAD_ERROR_OUT_OF_MEMORY Out of memory
 * @pre The download state must be #URL_DOWNLOAD_STATE_COMPLETED.
 * @see url_download_set_file_name()
 * @see url_download_set_destination()
 */
int url_download_get_downloaded_file(url_download_h download, char **path);


/**
 * @brief Gets the MIME type of the downloaded file
 *
 * @remarks This function returns #URL_DOWNLOAD_ERROR_INVALID_STATE if the download has not been started. \n
 * The @a mime_type must be released with free() by you.
 * @param [in] download The download handle
 * @param [out] mime_type The MIME type of the downloaded file
 * @return 0 on success, otherwise a negative error value.
 * @retval #URL_DOWNLOAD_ERROR_NONE Successful
 * @retval #URL_DOWNLOAD_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #URL_DOWNLOAD_ERROR_OUT_OF_MEMORY Out of memory
 * @see url_download_set_file_name()
 * @see url_download_set_destination()
 * @see url_download_get_downloaded_file()
 */
int url_download_get_mime(url_download_h download, char **mime_type);


/**
 * @brief Adds an HTTP header field to the download request
 *
 * @details The given HTTP header field will be included with the HTTP request of the download request. \n
 * Refer to the <a href="http://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.2">HTTP/1.1: HTTP Message Headers</a>
 * @remarks This function should be called before downloading (see url_download_start()) \n
 * This function replaces any existing value for the given key. \n
 * This function returns #URL_DOWNLOAD_ERROR_INVALID_PARAMETER if field or value is zero-length string. 
 * @param [in] download The download handle
 * @param [in] field The name of the HTTP header field
 * @param [in] value The value associated with given field
 * @return 0 on success, otherwise a negative error value.
 * @retval #URL_DOWNLOAD_ERROR_NONE Successful
 * @retval #URL_DOWNLOAD_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #URL_DOWNLOAD_ERROR_OUT_OF_MEMORY Out of memory
 * @retval #URL_DOWNLOAD_ERROR_INVALID_STATE Invalid state
 * @retval #URL_DOWNLOAD_ERROR_IO_ERROR Internal I/O error
 * @pre The download state must be #URL_DOWNLOAD_STATE_READY or #URL_DOWNLOAD_STATE_COMPLETED.
 * @see url_download_get_http_header_field()
 * @see url_download_remove_http_header_field()
 */
int url_download_add_http_header_field(url_download_h download, const char *field, const char *value);


/**
 * @brief Gets the value associated with given HTTP header field from the download
 *
 * @remarks This function returns #URL_DOWNLOAD_ERROR_INVALID_PARAMETER if field is zero-length string. \n
 * The @a value must be released with free() by you.
 * @param [in] download The download handle
 * @param [in] field The name of the HTTP header field
 * @param [out] value The value associated with given field
 * @return 0 on success, otherwise a negative error value.
 * @retval #URL_DOWNLOAD_ERROR_NONE Successful
 * @retval #URL_DOWNLOAD_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #URL_DOWNLOAD_ERROR_OUT_OF_MEMORY Out of memory
 * @retval #URL_DOWNLOAD_ERROR_INVALID_STATE Invalid state
 * @retval #URL_DOWNLOAD_ERROR_FIELD_NOT_FOUND Specified field not found
 * @see url_download_add_http_header_field()
 * @see url_download_remove_http_header_field()
 */
int url_download_get_http_header_field(url_download_h download, const char *field, char **value);


/**
 * @brief Removes the given HTTP header field from the download
 *
 * @remarks This function should be called before downloading (see url_download_start()) \n
 * This function returns #URL_DOWNLOAD_ERROR_INVALID_PARAMETER if field is zero-length string. 
 * @param [in] download The download handle
 * @param [in] field The name of the HTTP header field
 * @return 0 on success, otherwise a negative error value.
 * @retval #URL_DOWNLOAD_ERROR_NONE Successful
 * @retval #URL_DOWNLOAD_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #URL_DOWNLOAD_ERROR_OUT_OF_MEMORY Out of memory
 * @retval #URL_DOWNLOAD_ERROR_INVALID_STATE Invalid state
 * @retval #URL_DOWNLOAD_ERROR_FIELD_NOT_FOUND Specified field not found
 * @retval #URL_DOWNLOAD_ERROR_IO_ERROR Internal I/O error
 * @see url_download_add_http_header_field()
 * @see url_download_get_http_header_field()
 */
int url_download_remove_http_header_field(url_download_h download, const char *field);


/**
 * @brief Registers a callback function to be invoked when the download starts.
 *
 * @remarks This function should be called before downloading (see url_download_start())
 * @param [in] download The download handle
 * @param [in] callback The callback function to register
 * @param [in] user_data The user data to be passed to the callback function
 * @return 0 on success, otherwise a negative error value.
 * @retval #URL_DOWNLOAD_ERROR_NONE Successful
 * @retval #URL_DOWNLOAD_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #URL_DOWNLOAD_ERROR_INVALID_STATE Invalid state
 * @pre The download state must be #URL_DOWNLOAD_STATE_READY or #URL_DOWNLOAD_STATE_COMPLETED.
 * @post url_download_started_cb() will be invoked.
 * @see url_download_unset_started_cb()
 * @see url_download_started_cb()
*/
int url_download_set_started_cb(url_download_h download, url_download_started_cb callback, void* user_data);


/**
 * @brief Unregisters the callback function.
 *
 * @remarks This function should be called before downloading (see url_download_start())
 * @param [in] download The download handle
 * @return 0 on success, otherwise a negative error value.
 * @retval #URL_DOWNLOAD_ERROR_NONE Successful
 * @retval #URL_DOWNLOAD_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #URL_DOWNLOAD_ERROR_INVALID_STATE Invalid state
 * @pre The download state must be #URL_DOWNLOAD_STATE_READY or #URL_DOWNLOAD_STATE_COMPLETED.
 * @see url_download_set_started_cb()
 * @see url_download_started_cb()
*/
int url_download_unset_started_cb(url_download_h download);


/**
 * @brief Registers a callback function to be invoked when the download is paused.
 *
 * @remarks This function should be called before downloading (see url_download_start())
 * @param [in] download The download handle
 * @param [in] callback The callback function to register
 * @param [in] user_data The user data to be passed to the callback function
 * @return 0 on success, otherwise a negative error value.
 * @retval #URL_DOWNLOAD_ERROR_NONE Successful
 * @retval #URL_DOWNLOAD_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #URL_DOWNLOAD_ERROR_INVALID_STATE Invalid state
 * @pre The download state must be #URL_DOWNLOAD_STATE_READY or #URL_DOWNLOAD_STATE_COMPLETED.
 * @post url_download_paused_cb() will be invoked.
 * @see url_download_unset_paused_cb()
 * @see url_download_paused_cb()
*/
int url_download_set_paused_cb(url_download_h download, url_download_paused_cb callback, void* user_data);


/**
 * @brief Unregisters the callback function.
 *
 * @remarks This function should be called before downloading (see url_download_start())
 * @param [in] download The download handle
 * @return 0 on success, otherwise a negative error value.
 * @retval #URL_DOWNLOAD_ERROR_NONE Successful
 * @retval #URL_DOWNLOAD_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #URL_DOWNLOAD_ERROR_INVALID_STATE Invalid state
 * @pre The download state must be #URL_DOWNLOAD_STATE_READY or #URL_DOWNLOAD_STATE_COMPLETED.
 * @see url_download_set_paused_cb()
 * @see url_download_paused_cb()
*/
int url_download_unset_paused_cb(url_download_h download);


/**
 * @brief Registers a callback function to be invoked when the download completed.
 *
 * @remarks This function should be called before downloading (see url_download_start())
 * @param [in] download The download handle
 * @param [in] callback The callback function to register
 * @param [in] user_data The user data to be passed to the callback function
 * @return 0 on success, otherwise a negative error value.
 * @retval #URL_DOWNLOAD_ERROR_NONE Successful
 * @retval #URL_DOWNLOAD_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #URL_DOWNLOAD_ERROR_INVALID_STATE Invalid state
 * @pre The download state must be #URL_DOWNLOAD_STATE_READY or #URL_DOWNLOAD_STATE_COMPLETED.
 * @post url_download_completed_cb() will be invoked.
 * @see url_download_unset_completed_cb()
 * @see url_download_completed_cb()
*/
int url_download_set_completed_cb(url_download_h download, url_download_completed_cb callback, void* user_data);


/**
 * @brief Unregisters the callback function.
 *
 * @remarks This function should be called before downloading (see url_download_start())
 * @param [in] download The download handle
 * @return 0 on success, otherwise a negative error value.
 * @retval #URL_DOWNLOAD_ERROR_NONE Successful
 * @retval #URL_DOWNLOAD_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #URL_DOWNLOAD_ERROR_INVALID_STATE Invalid state
 * @pre The download state must be #URL_DOWNLOAD_STATE_READY or #URL_DOWNLOAD_STATE_COMPLETED.
 * @see url_download_set_completed_cb()
 * @see url_download_completed_cb()
*/
int url_download_unset_completed_cb(url_download_h download);


/**
 * @brief Registers a callback function to be invoked when the download is stopped.
 *
 * @remarks This function should be called before downloading (see url_download_start())
 * @param [in] download The download handle
 * @param [in] callback The callback function to register
 * @param [in] user_data The user data to be passed to the callback function
 * @return 0 on success, otherwise a negative error value.
 * @retval #URL_DOWNLOAD_ERROR_NONE Successful
 * @retval #URL_DOWNLOAD_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #URL_DOWNLOAD_ERROR_INVALID_STATE Invalid state
 * @pre The download state must be #URL_DOWNLOAD_STATE_READY or #URL_DOWNLOAD_STATE_COMPLETED.
 * @post url_download_stopped_cb() will be invoked.
 * @see url_download_unset_stopped_cb()
 * @see url_download_stopped_cb()
*/
int url_download_set_stopped_cb(url_download_h download, url_download_stopped_cb callback, void* user_data);


/**
 * @brief Unregisters the callback function.
 *
 * @remarks This function should be called before downloading (see url_download_start())
 * @param [in] download The download handle
 * @return 0 on success, otherwise a negative error value.
 * @retval #URL_DOWNLOAD_ERROR_NONE Successful
 * @retval #URL_DOWNLOAD_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #URL_DOWNLOAD_ERROR_INVALID_STATE Invalid state
 * @pre The download state must be #URL_DOWNLOAD_STATE_READY or #URL_DOWNLOAD_STATE_COMPLETED.
 * @see url_download_set_stopped_cb()
 * @see url_download_stopped_cb()
*/
int url_download_unset_stopped_cb(url_download_h download);


/**
 * @brief Registers a callback function to be invoked when progress of the download changes
 *
 * @remarks This function should be called before downloading (see url_download_start())
 * @param [in] download The download handle
 * @param [in] callback The callback function to register
 * @param [in] user_data The user data to be passed to the callback function
 * @return 0 on success, otherwise a negative error value.
 * @retval #URL_DOWNLOAD_ERROR_NONE Successful
 * @retval #URL_DOWNLOAD_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #URL_DOWNLOAD_ERROR_INVALID_STATE Invalid state
 * @pre The download state must be #URL_DOWNLOAD_STATE_READY or #URL_DOWNLOAD_STATE_COMPLETED.
 * @post url_download_progress_cb() will be invoked.
 * @see url_download_unset_progress_cb()
 * @see url_download_progress_cb()
*/
int url_download_set_progress_cb(url_download_h download, url_download_progress_cb callback, void *user_data);


/**
 * @brief Unregisters the callback function.
 *
 * @remarks This function should be called before downloading (see url_download_start())
 * @param [in] download The download handle
 * @return 0 on success, otherwise a negative error value.
 * @retval #URL_DOWNLOAD_ERROR_NONE Successful
 * @retval #URL_DOWNLOAD_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #URL_DOWNLOAD_ERROR_INVALID_STATE Invalid state
 * @pre The download state must be #URL_DOWNLOAD_STATE_READY or #URL_DOWNLOAD_STATE_COMPLETED.
 * @see url_download_set_progress_cb()
 * @see url_download_progress_cb()
*/
int url_download_unset_progress_cb(url_download_h download);


/**
 * @brief Starts or resumes the download, asynchronously.
 *
 * @details This function starts to download the current URL, or resumes the download if paused.
 *
 * @remarks The URL is the mandatory information to start the download.
 * @param [in] download The download handle
 * @param [out] id The identifier for the download unique within the application.
 * @return 0 on success, otherwise a negative error value.
 * @retval #URL_DOWNLOAD_ERROR_NONE Successful
 * @retval #URL_DOWNLOAD_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #URL_DOWNLOAD_ERROR_OUT_OF_MEMORY Out of memory
 * @retval #URL_DOWNLOAD_ERROR_INVALID_STATE Invalid state
 * @retval #URL_DOWNLOAD_ERROR_IO_ERROR Internal I/O error
 * @retval #URL_DOWNLOAD_ERROR_URL Invalid URL
 * @retval #URL_DOWNLOAD_ERROR_DESTINATION Invalid destination
 * @pre The download state must be #URL_DOWNLOAD_STATE_READY, #URL_DOWNLOAD_STATE_PAUSED or #URL_DOWNLOAD_STATE_COMPLETED.
 * @post The download state will be #URL_DOWNLOAD_STATE_DOWNLOADING
 * @see url_download_set_url()
 * @see url_download_pause()
 * @see url_download_stop()
 * @see url_download_set_started_cb()
 * @see url_download_unset_started_cb()
 * @see url_download_started_cb()
 */
int url_download_start(url_download_h download, int *id);


/**
 * @brief Pauses the download, asynchronously.
 *
 * @remarks The paused download can be restarted with url_download_start() or canceled with url_download_stop()
 * @param [in] download The download handle
 * @return 0 on success, otherwise a negative error value.
 * @retval #URL_DOWNLOAD_ERROR_NONE Successful
 * @retval #URL_DOWNLOAD_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #URL_DOWNLOAD_ERROR_OUT_OF_MEMORY Out of memory
 * @retval #URL_DOWNLOAD_ERROR_INVALID_STATE Invalid state
 * @retval #URL_DOWNLOAD_ERROR_IO_ERROR Internal I/O error
 * @pre The download state must be #URL_DOWNLOAD_STATE_DOWNLOADING.
 * @post The download state will be #URL_DOWNLOAD_STATE_PAUSED.
 * @see url_download_start()
 * @see url_download_stop()
 * @see url_download_set_paused_cb()
 * @see url_download_unset_paused_cb()
 * @see url_download_paused_cb()
 */
int url_download_pause(url_download_h download);

/**
 * @brief Stops the download, asynchronously.
 *
 * @details This function cancels the running download and its state will be #URL_DOWNLOAD_STATE_READY
 * @remarks The stopped download can be restarted with url_download_start().
 * @param [in] download The download handle
 * @return 0 on success, otherwise a negative error value.
 * @retval #URL_DOWNLOAD_ERROR_NONE Successful
 * @retval #URL_DOWNLOAD_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #URL_DOWNLOAD_ERROR_OUT_OF_MEMORY Out of memory
 * @retval #URL_DOWNLOAD_ERROR_INVALID_STATE Invalid state
 * @retval #URL_DOWNLOAD_ERROR_IO_ERROR Internal I/O error
 * @pre The download state must be #URL_DOWNLOAD_STATE_DOWNLOADING.
 * @post url_download_stopped_cb() will be invoked if it is registered with url_download_set_stopped_cb()
 * @post The download state will be #URL_DOWNLOAD_STATE_READY.
 * @see url_download_start()
 * @see url_download_stop()
 * @see url_download_set_stopped_cb()
 * @see url_download_unset_stopped_cb()
 * @see url_download_stopped_cb()
 */
int url_download_stop(url_download_h download);

/**
 * @brief Gets the download's current state.
 *
 * @param [in] download The download handle
 * @param [out] state The current state of the download
 * @return 0 on success, otherwise a negative error value.
 * @retval #URL_DOWNLOAD_ERROR_NONE Successful
 * @retval #URL_DOWNLOAD_ERROR_INVALID_PARAMETER Invalid parameter
 * @see #url_download_state_e
 */
int url_download_get_state(url_download_h download, url_download_state_e *state);

/**
 * @brief Retrieves all HTTP header fields to be included with the download
 * @details This function calls url_download_http_header_field_cb() once for each HTTP header field added.\n
 * If url_download_http_header_field_cb() callback function returns false, then iteration will be finished.
 *
 * @param [in] download The download handle
 * @param [in] callback The iteration callback function
 * @param [in] user_data The user data to be passed to the callback function
 * @return 0 on success, otherwise a negative error value.
 * @retval #URL_DOWNLOAD_ERROR_NONE Successful
 * @retval #URL_DOWNLOAD_ERROR_INVALID_PARAMETER Invalid parameter
 * @post This function invokes url_download_http_header_field_cb().
 * @see url_download_http_header_field_cb()
 */
int url_download_foreach_http_header_field(url_download_h download, url_download_http_header_field_cb callback, void *user_data);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* __TIZEN_WEB_URL_DOWNLOAD_H__ */
