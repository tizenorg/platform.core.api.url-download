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


#ifndef __TIZEN_WEB_URL_DOWNLOAD_PRIVATE_H__
#define __TIZEN_WEB_URL_DOWNLOAD_PRIVATE_H__

#include <bundle.h>
#ifndef ENABLE_DOWNLOAD_PROVIDER
#include <download-agent-interface.h>
#endif

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef ENABLE_DOWNLOAD_PROVIDER
typedef da_client_cb_t *url_download_agent_h;
#endif

/**
 * url_download_cb_s
 */
struct url_download_cb_s {
	url_download_started_cb started;
	void *started_user_data;

	url_download_paused_cb paused;
	void *paused_user_data;

	url_download_completed_cb completed;
	void *completed_user_data;

	url_download_stopped_cb stopped;
	void *stopped_user_data;

	url_download_progress_cb progress;
	void *progress_user_data;
};

struct url_download_s {
#ifndef ENABLE_DOWNLOAD_PROVIDER
	url_download_agent_h agent;
	da_handle_t id;
#else
	uint id;
	uint enable_notification;
	int requestid;
#endif
	struct url_download_cb_s callback;
	url_download_state_e state;
	char *url;
	char *destination;
	bundle *http_header;
	char *completed_path;
	char *content_name;
	char *mime_type;
	bundle_raw *service_data;
	int service_data_len;
	uint file_size;
	int sockfd;
	int slot_index;
};

#define MAX_DOWNLOAD_HANDLE_COUNT 5

#ifdef __cplusplus
}
#endif

#endif /* __TIZEN_WEB_URL_DOWNLOAD_PRIVATE_H__ */
