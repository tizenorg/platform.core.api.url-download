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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dlog.h>
#include <download-agent-interface.h>

#include <url_download.h>
#include <url_download_private.h>

#ifdef LOG_TAG
#undef LOG_TAG
#endif

#define LOG_TAG "TIZEN_N_URL_DOWNLOAD"


#define STATE_IS_RUNNING(_download_) \
	 (_download_->state == URL_DOWNLOAD_STATE_DOWNLOADING \
	 || _download_->state == URL_DOWNLOAD_STATE_PAUSED)


#define STRING_IS_INVALID(_string_) \
	(_string_ == NULL || _string_[0] == '\0')


static bool DBG_AGENT = true;
static bool DBG_HTTP_HEADER = true;
static bool DBG_DOWNLOAD = true;

static void url_download_agent_state_cb(user_notify_info_t *notify_info, void* user_param);
static void url_download_agent_progress_cb(user_download_info_t *download_info,void* user_param);
static int url_download_start_resume(url_download_h download);
static int url_download_start_download(url_download_h download);


static const char* url_download_error_to_string(int error_code)
{
	char *error_name = NULL;

	switch (error_code)
	{
	case URL_DOWNLOAD_ERROR_NONE:
		error_name = "ERROR_NONE";
		break;
	case URL_DOWNLOAD_ERROR_INVALID_PARAMETER:
		error_name = "INVALID_PARAMETER";
		break;
	case URL_DOWNLOAD_ERROR_OUT_OF_MEMORY:
		error_name = "OUT_OF_MEMORY";
		break;
	case URL_DOWNLOAD_ERROR_IO_ERROR:
		error_name = "IO_ERROR";
		break;
	case URL_DOWNLOAD_ERROR_NETWORK_UNREACHABLE:
		error_name = "NETWORK_UNREACHABLE";
		break;
	case URL_DOWNLOAD_ERROR_CONNECTION_TIMED_OUT:
		error_name = "CONNECTION_TIMED_OUT";
		break;

	case URL_DOWNLOAD_ERROR_FIELD_NOT_FOUND:
		error_name = "FIELD_NOT_FOUND";
		break;

	case URL_DOWNLOAD_ERROR_NO_SPACE:
		error_name = "NO_SPACE";
		break;
	case URL_DOWNLOAD_ERROR_INVALID_STATE:
		error_name = "INVALID_STATE";
		break;
	case URL_DOWNLOAD_ERROR_CONNECTION_FAILED:
		error_name = "CONNECTION_FAILED";
		break;
	case URL_DOWNLOAD_ERROR_SSL_FAILED:
		error_name = "SSL_FAILED";
		break;

	case URL_DOWNLOAD_ERROR_INVALID_URL:
		error_name = "INVALID_URL";
		break;

	case URL_DOWNLOAD_ERROR_INVALID_DESTINATION:
		error_name = "INVALID_DESTINATION";
		break;

	default:
		error_name = "UNKNOWN";
		break;
	}
	return error_name;
}

static int url_download_error(const char *function, int error_code, const char *description)
{
	const char *error_name = NULL;

	error_name = url_download_error_to_string(error_code);

	if (description)
	{
		LOGE("[%s] %s(0x%08x) : %s", function, error_name, error_code, description);	
	}
	else
	{
		LOGE("[%s] %s(0x%08x)", function, error_name, error_code);	
	}

	return error_code;
}

static const char* url_download_state_to_string(url_download_state_e state)
{
	switch (state)
	{
	case URL_DOWNLOAD_STATE_READY:
		return "READY";

	case URL_DOWNLOAD_STATE_DOWNLOADING:
		return "DOWNLOADING";

	case URL_DOWNLOAD_STATE_PAUSED:
		return "PAUSED";

	case URL_DOWNLOAD_STATE_COMPLETED:
		return "COMPLETED";

	default:
		return "INVALID";
	}
}

static int url_download_error_invalid_state(const char *function, url_download_h download)
{
	LOGE("[%s] INVALID_STATE(0x%08x) : state(%s)",
		 function, URL_DOWNLOAD_ERROR_INVALID_STATE, url_download_state_to_string(download->state));	

	return URL_DOWNLOAD_ERROR_INVALID_STATE;
}

struct _download_list_t {
	url_download_h data;
	struct _download_list_t *next;
};
static int _download_agent_reference = 0;
static da_client_cb_t *_download_agent = NULL;
static struct _download_list_t *_download_list = NULL;

static int url_download_agent_create(url_download_agent_h *agent)
{
	int retcode;

	if (_download_agent == NULL)
	{
		_download_agent = calloc(1, sizeof(da_client_cb_t));

		if (!_download_agent)
		{
			return URL_DOWNLOAD_ERROR_OUT_OF_MEMORY;
		}

		_download_agent->user_noti_cb = url_download_agent_state_cb;
		_download_agent->update_dl_info_cb = url_download_agent_progress_cb;
		_download_agent->send_dd_info_cb = NULL;
		_download_agent->go_to_next_url_cb = NULL;

		retcode = da_init(_download_agent, DA_DOWNLOAD_MANAGING_METHOD_AUTO);
	
		if (retcode)
		{
			free(_download_agent);
			_download_agent = NULL;
			return URL_DOWNLOAD_ERROR_IO_ERROR;
		}

		LOGI_IF(DBG_AGENT, "[%s] download-agent is created", __FUNCTION__);

	}

	_download_agent_reference++;

	*agent = _download_agent;

	LOGI_IF(DBG_AGENT, "[%s] download-agent reference(%d)", __FUNCTION__, _download_agent_reference);

	return URL_DOWNLOAD_ERROR_NONE;
}

static void url_download_agent_destroy(url_download_agent_h agent)
{
	if (agent == NULL || agent != _download_agent)
	{
		url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);
		return;
	}

	_download_agent_reference--;
	
	if (_download_agent_reference <= 0)
	{
		da_deinit();

		free(_download_agent);
		_download_agent = NULL;

		_download_agent_reference = 0;

		LOGI_IF(DBG_AGENT, "[%s] download-agent is destroyed", __FUNCTION__);	
	}
	
	LOGI_IF(DBG_AGENT, "[%s] download-agent reference(%d)", __FUNCTION__, _download_agent_reference);

}

static url_download_error_e url_download_agent_error(int error)
{
	switch (error)
	{
	case DA_RESULT_OK:
		return URL_DOWNLOAD_ERROR_NONE;

	case DA_ERR_NETWORK_FAIL:
		return URL_DOWNLOAD_ERROR_CONNECTION_FAILED;

	case DA_ERR_UNREACHABLE_SERVER:
		return URL_DOWNLOAD_ERROR_NETWORK_UNREACHABLE;

	case DA_ERR_HTTP_TIMEOUT:
		return URL_DOWNLOAD_ERROR_CONNECTION_TIMED_OUT;

	case DA_ERR_SSL_FAIL:
		return URL_DOWNLOAD_ERROR_SSL_FAILED;

	case DA_ERR_FAIL_TO_ACCESS_FILE:
	case DA_ERR_FAIL_TO_ACCESS_STORAGE:
	case DA_ERR_FAIL_TO_INSTALL_FILE:
		return URL_DOWNLOAD_ERROR_INVALID_DESTINATION;

	case DA_ERR_DISK_FULL:
		return URL_DOWNLOAD_ERROR_NO_SPACE;

	case DA_ERR_INVALID_URL:
	case DA_ERR_UNSUPPORTED_PROTOCAL:
		return URL_DOWNLOAD_ERROR_INVALID_URL;

	case DA_ERR_INVALID_INSTALL_PATH:
		return URL_DOWNLOAD_ERROR_INVALID_DESTINATION;

	default:
		return URL_DOWNLOAD_ERROR_IO_ERROR;
	}
}


static int url_download_agent_dispatch_state_change(int da_state, url_download_state_e prev_state, url_download_state_e *next_state)
{
	switch (da_state)
	{
	case DA_STATE_DOWNLOAD_STARTED:
	case DA_STATE_RESUMED:
		*next_state = URL_DOWNLOAD_STATE_DOWNLOADING;
		return URL_DOWNLOAD_ERROR_NONE;

	case DA_STATE_FINISHED:
		*next_state = URL_DOWNLOAD_STATE_COMPLETED;
		return URL_DOWNLOAD_ERROR_NONE;

	case DA_STATE_CANCELED:
	case DA_STATE_FAILED:
		*next_state = URL_DOWNLOAD_STATE_READY;
		return URL_DOWNLOAD_ERROR_NONE;

	case DA_STATE_SUSPENDED:
		*next_state = URL_DOWNLOAD_STATE_PAUSED;
		return URL_DOWNLOAD_ERROR_NONE;

	case DA_STATE_DOWNLOADING:
		if (prev_state == URL_DOWNLOAD_STATE_PAUSED)
		{
			*next_state = URL_DOWNLOAD_STATE_DOWNLOADING;
			return URL_DOWNLOAD_ERROR_NONE;
		}
		return URL_DOWNLOAD_ERROR_INVALID_STATE;

	default:
		return URL_DOWNLOAD_ERROR_INVALID_STATE;
	}
}


static bool is_available_download_data(url_download_h download)
{
	struct _download_list_t *head = NULL;
	bool ret = false;
	head = _download_list;
	while(head)
	{
		if (head->data == download)
		{
			ret = true;
			break;
		}
		head = head->next;
	}
	return ret;
}

static void url_download_agent_state_cb(user_notify_info_t *notify_info, void* user_data)
{
	url_download_h download = NULL;
	url_download_state_e state = -1;
	url_download_error_e stopped_code = URL_DOWNLOAD_ERROR_NONE;

	if (user_data == NULL)
	{
		url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_IO_ERROR, "failed to get the download handle");
		return;
	}

	download = (url_download_h)user_data;
	if (!is_available_download_data(download))
	{
		url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, "download item is already destroyed");
		return;
	}
/*
	if (download->agent == NULL)
	{
		url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_IO_ERROR, "failed to get the download agent handle");
		return;
	}
*/
	if (url_download_agent_dispatch_state_change(notify_info->state, download->state, &state))
	{
		LOGI_IF(DBG_AGENT, "[%s] id[%d]no need to dispatch state change : da-state(%d)", __FUNCTION__, download->id, notify_info->state);
		return;
	}

	LOGI_IF(DBG_AGENT, "[%s] id(%d), state(%s)", __FUNCTION__, download->id, url_download_state_to_string(state));

	switch (state)
	{
	case URL_DOWNLOAD_STATE_READY:
		download->state = URL_DOWNLOAD_STATE_READY;
		if (download->callback.stopped)
		{
			stopped_code = url_download_agent_error(notify_info->err);
			LOGI_IF(DBG_AGENT, "[%s] id(%d), stopped by [%s]",
				 __FUNCTION__, download->id, url_download_error_to_string(stopped_code));
			download->callback.stopped(download,stopped_code, download->callback.stopped_user_data);
		}
		return;

	case URL_DOWNLOAD_STATE_DOWNLOADING:
		download->state = URL_DOWNLOAD_STATE_DOWNLOADING;
		if (download->callback.started)
		{
			download->callback.started(download, download->callback.started_user_data);
		}
		return;

	case URL_DOWNLOAD_STATE_PAUSED:
		download->state = URL_DOWNLOAD_STATE_PAUSED;
		if (download->callback.paused)
		{
			download->callback.paused(download, download->callback.paused_user_data);
		}
		return;

	case URL_DOWNLOAD_STATE_COMPLETED:
		download->state = URL_DOWNLOAD_STATE_COMPLETED;
		if (download->callback.completed)
		{
			download->callback.completed(download, download->completed_path, download->callback.completed_user_data);
		}
		return;

	default:
		url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_IO_ERROR, "invalid state change event");
		return;
	}

}


static void url_download_agent_progress_cb(user_download_info_t *download_info,void* user_data)
{
	url_download_h download;

	if (user_data == NULL)
	{
		url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_IO_ERROR, "failed to get the download handle");
		return;
	}

	download = (url_download_h)user_data;

	if (!is_available_download_data(download))
	{
		url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, "download item is already destroyed");
		return;
	}

	if (download_info->saved_path != NULL)
	{
		download->completed_path = strdup(download_info->saved_path);
	}
	
	if (download->callback.progress)
	{
		download->callback.progress(
				download,
				download_info->total_received_size, download_info->file_size,
				download->callback.progress_user_data);
	}
}


int url_download_create(url_download_h *download)
{
	url_download_agent_h agent;
	url_download_h download_new;
	struct _download_list_t *head = NULL;
	struct _download_list_t *temp_item = NULL;

	if (download == NULL)
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);
	}

	download_new = (url_download_h)calloc(1, sizeof(struct url_download_s));

	if (download_new == NULL)
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_OUT_OF_MEMORY, NULL);
	}
	
	if (url_download_agent_create(&agent))
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_IO_ERROR, "failed to initialize an agent");
	}

	download_new->agent = agent;

	download_new->http_header = bundle_create();

	if (!download_new->http_header)
	{
		url_download_destroy(download_new);
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_IO_ERROR, "failed to initialize a bundle");
	}

	download_new->state = URL_DOWNLOAD_STATE_READY;
	*download = download_new;
	if (_download_list == NULL)
	{
		_download_list = calloc(1, sizeof(struct _download_list_t));
		if (_download_list == NULL)
		{
			return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_OUT_OF_MEMORY, NULL);
		}
		_download_list->data = download_new;
	} else {
		head = _download_list;
		while (head->next)
		{
			head = head->next;
		}
		temp_item = calloc(1, sizeof(struct _download_list_t));
		if (temp_item == NULL) {
			return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_OUT_OF_MEMORY, NULL);
		}
		temp_item->data = download_new;
		head->next = temp_item;
	}

	return URL_DOWNLOAD_ERROR_NONE;
}

int url_download_destroy(url_download_h download)
{
	struct _download_list_t *head = NULL;
	struct _download_list_t *prev = NULL;
	if (download == NULL)
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);
	}

	if (STATE_IS_RUNNING(download))
	{
		url_download_stop(download);
	}

	if (download->url)
	{
		free(download->url);
		download->url = NULL;
	}

	if (download->destination)
	{
		free(download->destination);
		download->destination = NULL;
	}

	if (download->http_header)
	{
		bundle_free(download->http_header);
		download->http_header = NULL;
	}

	if (download->completed_path)
	{
		free(download->completed_path);
		download->completed_path = NULL;
	}

	url_download_agent_destroy(download->agent);

	head = _download_list;
	while (head)
	{
		if (head->data && head->data == download)
		{
			head->data = NULL;
			if (prev)
			{
				if (head->next)
					prev->next = head->next;
				else
					prev->next = NULL;
			}
			else
			{
				if (head->next)
					_download_list = head->next;
				else
					_download_list = NULL;
			}
			free(head);
			head = NULL;
			break;
		}
		prev = head;
		head = head->next;
	}

	free(download);
	download = NULL;

	return URL_DOWNLOAD_ERROR_NONE;
}


int url_download_set_url(url_download_h download, const char *url)
{
	char *url_dup = NULL;

	if (download == NULL)
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);	
	}

	if (STATE_IS_RUNNING(download))
	{
		return url_download_error_invalid_state(__FUNCTION__, download);
	}

	if (url != NULL)
	{
		url_dup = strdup(url);

		if (url_dup == NULL)
		{
			return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_OUT_OF_MEMORY, NULL);
		}
	}

	if (download->url != NULL)
	{
		free(download->url);
	}

	download->url = url_dup;

	return URL_DOWNLOAD_ERROR_NONE;
}


int url_download_get_url(url_download_h download, char **url)
{
	char *url_dup = NULL;

	if (download == NULL || url == NULL)
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);
	}

	if (download->url != NULL)
	{
		url_dup = strdup(download->url);

		if (url_dup == NULL)
		{
			return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_OUT_OF_MEMORY, NULL);
		}
	}

	*url = url_dup;

	return URL_DOWNLOAD_ERROR_NONE;
}


int url_download_set_destination(url_download_h download, const char *path)
{
	char *path_dup = NULL;

	if (download == NULL)
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);
	}

	if (STATE_IS_RUNNING(download))
	{
		return url_download_error_invalid_state(__FUNCTION__, download);
	}

	if (path != NULL)
	{
		path_dup = strdup(path);

		if (path_dup == NULL)
		{
			return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_OUT_OF_MEMORY, NULL);
		}
	}

	if (download->destination != NULL)
	{
		free(download->destination);
	}

	download->destination = path_dup;

	return URL_DOWNLOAD_ERROR_NONE;
}


int url_download_get_destination(url_download_h download, char **path)
{
	char *path_dup = NULL;

	if (download == NULL || path == NULL)
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);
	}

	if (download->destination != NULL)
	{
		path_dup = strdup(download->destination);

		if (path_dup == NULL)
		{
			return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_OUT_OF_MEMORY, NULL);
		}
	}

	*path = path_dup;

	return URL_DOWNLOAD_ERROR_NONE;
}


int url_download_add_http_header_field(url_download_h download, const char *field, const char *value)
{
	if (download == NULL)
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);
	}

	if (STRING_IS_INVALID(field) || STRING_IS_INVALID(value))
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);
	}

	if (STATE_IS_RUNNING(download))
	{
		return url_download_error_invalid_state(__FUNCTION__, download);
	}

	if (bundle_get_val(download->http_header, field))
	{
		bundle_del(download->http_header, field);
	}

	if (bundle_add(download->http_header, field, value))
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_IO_ERROR, NULL);
	}

	return URL_DOWNLOAD_ERROR_NONE;
}


int url_download_get_http_header_field(url_download_h download, const char *field, char **value)
{
	const char *bundle_value;
	char *field_value_dup;

	if (download == NULL || value == NULL)
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);
	}

	if (STRING_IS_INVALID(field))
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);
	}

	bundle_value = bundle_get_val(download->http_header, field);

	if (bundle_value == NULL)
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_FIELD_NOT_FOUND, NULL);
	}

	field_value_dup = strdup(bundle_value);

	if (field_value_dup == NULL)
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_OUT_OF_MEMORY, NULL);
	}

	*value = field_value_dup;

	return URL_DOWNLOAD_ERROR_NONE;
}


int url_download_remove_http_header_field(url_download_h download, const char *field)
{
	if (download == NULL)
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);
	}

	if (STRING_IS_INVALID(field))
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, "invalid field");
	}

	if (STATE_IS_RUNNING(download))
	{
		return url_download_error_invalid_state(__FUNCTION__, download);
	}

	if (!bundle_get_val(download->http_header, field))
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_FIELD_NOT_FOUND, NULL);
	}

	if (bundle_del(download->http_header, field))
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_IO_ERROR, NULL);
	}

	return URL_DOWNLOAD_ERROR_NONE;
}

typedef struct http_field_array_s{
	char **array;
	int array_length;
	int position;
} http_field_array_t;

static void url_download_get_all_http_header_fields_iterator(const char *field_name, const char *field_value, void *user_data)
{
	http_field_array_t *http_field_array;
	char *field_buffer;
	int field_buffer_length;
	const char *field_delimiters = ": ";

	http_field_array = user_data;

	if (http_field_array == NULL)
	{
		url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);
		return;
	}

	// REF : http://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.2
	field_buffer_length = strlen(field_name) + strlen(field_delimiters) + strlen(field_value) + 1;
	
	field_buffer = calloc(field_buffer_length, sizeof(char));

	if (field_buffer == NULL)
	{
		url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_OUT_OF_MEMORY, NULL);
		return;
	}

	snprintf(field_buffer, field_buffer_length, "%s%s%s", field_name, field_delimiters, field_value);

	http_field_array->array[http_field_array->position] = field_buffer;
	http_field_array->position++;

}

static int url_download_get_all_http_header_fields(url_download_h download, char ***fields, int *fields_length)
{
	http_field_array_t http_field_array;

	if (download == NULL || fields == NULL || fields_length == NULL)
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);
	}
	
	http_field_array.position = 0;
	http_field_array.array_length = bundle_get_count(download->http_header);
	http_field_array.array = calloc(http_field_array.array_length, sizeof(char*));

	if (http_field_array.array == NULL)
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_OUT_OF_MEMORY, NULL);
	}

	bundle_iterate(download->http_header, url_download_get_all_http_header_fields_iterator, &http_field_array);

	if (DBG_HTTP_HEADER)
	{
		int i=0;

		for (i=0; i<http_field_array.position; i++)
		{
			LOGI("[%s] header[%d] = [%s]", __FUNCTION__, i, http_field_array.array[i]);
		}
	}

	*fields = http_field_array.array;
	*fields_length = http_field_array.array_length;

	return URL_DOWNLOAD_ERROR_NONE;
}


int url_download_set_started_cb(url_download_h download, url_download_started_cb callback, void* user_data)
{
	if (download == NULL || callback == NULL)
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);
	}

	if (STATE_IS_RUNNING(download))
	{
		return url_download_error_invalid_state(__FUNCTION__, download);
	}

	download->callback.started = callback;
	download->callback.started_user_data = user_data;

	return URL_DOWNLOAD_ERROR_NONE;
}


int url_download_unset_started_cb(url_download_h download)
{
	if (download == NULL)
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);
	}

	if (STATE_IS_RUNNING(download))
	{
		return url_download_error_invalid_state(__FUNCTION__, download);
	}

	download->callback.started = NULL;
	download->callback.started_user_data = NULL;

	return URL_DOWNLOAD_ERROR_NONE;
}


int url_download_set_paused_cb(url_download_h download, url_download_paused_cb callback, void* user_data)
{
	if (download == NULL || callback == NULL)
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);
	}

	if (STATE_IS_RUNNING(download))
	{
		return url_download_error_invalid_state(__FUNCTION__, download);
	}

	download->callback.paused = callback;
	download->callback.paused_user_data = user_data;

	return URL_DOWNLOAD_ERROR_NONE;
}


int url_download_unset_paused_cb(url_download_h download)
{
	if (download == NULL)
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);
	}

	if (STATE_IS_RUNNING(download))
	{
		return url_download_error_invalid_state(__FUNCTION__, download);
	}

	download->callback.paused = NULL;
	download->callback.paused_user_data = NULL;

	return URL_DOWNLOAD_ERROR_NONE;
}


int url_download_set_completed_cb(url_download_h download, url_download_completed_cb callback, void* user_data)
{
	if (download == NULL || callback == NULL)
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);
	}

	if (STATE_IS_RUNNING(download))
	{
		return url_download_error_invalid_state(__FUNCTION__, download);
	}

	download->callback.completed = callback;
	download->callback.completed_user_data = user_data;

	return URL_DOWNLOAD_ERROR_NONE;
}


int url_download_unset_completed_cb(url_download_h download)
{
	if (download == NULL)
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);
	}

	if (STATE_IS_RUNNING(download))
	{
		return url_download_error_invalid_state(__FUNCTION__, download);
	}

	download->callback.completed = NULL;
	download->callback.completed_user_data = NULL;

	return URL_DOWNLOAD_ERROR_NONE;
}


int url_download_set_stopped_cb(url_download_h download, url_download_stopped_cb callback, void* user_data)
{
	if (download == NULL || callback == NULL)
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);
	}

	if (STATE_IS_RUNNING(download))
	{
		return url_download_error_invalid_state(__FUNCTION__, download);
	}

	download->callback.stopped = callback;
	download->callback.stopped_user_data = user_data;

	return URL_DOWNLOAD_ERROR_NONE;
}


int url_download_unset_stopped_cb(url_download_h download)
{
	if (download == NULL)
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);
	}

	if (STATE_IS_RUNNING(download))
	{
		return url_download_error_invalid_state(__FUNCTION__, download);
	}

	download->callback.stopped = NULL;
	download->callback.stopped_user_data = NULL;

	return URL_DOWNLOAD_ERROR_NONE;
}


int url_download_set_progress_cb(url_download_h download, url_download_progress_cb callback, void *user_data)
{
	if (download == NULL || callback == NULL)
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);
	}

	if (STATE_IS_RUNNING(download))
	{
		return url_download_error_invalid_state(__FUNCTION__, download);
	}

	download->callback.progress = callback;
	download->callback.progress_user_data = user_data;

	return URL_DOWNLOAD_ERROR_NONE;
}


int url_download_unset_progress_cb(url_download_h download)
{
	if (download == NULL)
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);
	}

	if (STATE_IS_RUNNING(download))
	{
		return url_download_error_invalid_state(__FUNCTION__, download);
	}

	download->callback.progress = NULL;
	download->callback.progress_user_data = NULL;

	return URL_DOWNLOAD_ERROR_NONE;
}

static int url_download_start_download(url_download_h download)
{
	enum {
		FEATURE_DEFAULT = 0,
		FEATURE_DESTINATION = 1,
		FEATURE_HTTP = 2,
		FEATURE_DESTINATION_HTTP = 3,
	};

	int retcode;
	int feature_set = 0;
	int feature_oma_off = DA_FEATURE_OFF;

	char **http_headers = NULL;
	int http_headers_length = 0;

	if (download == NULL)
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);
	}

	download->id = -1;
	download->completed_path = NULL;

	if (STRING_IS_INVALID(download->url))
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, "URL is invalid");
	}

	if (download->destination != NULL)
	{
		feature_set += FEATURE_DESTINATION;
	}

	if (bundle_get_count(download->http_header) > 0)
	{
		feature_set += FEATURE_HTTP;
		
		if (url_download_get_all_http_header_fields(download, &http_headers, &http_headers_length))
		{
			return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_IO_ERROR, "Failed to set HTTP headers");
		}
	}

	LOGI_IF(DBG_DOWNLOAD, "[%s] try to start download : URL(%s) feature(%d)", __FUNCTION__, download->url, feature_set);
	
	switch (feature_set)
	{
	case FEATURE_DEFAULT:
		retcode = da_start_download_with_extension(
			download->url,
			&(download->id),
			DA_FEATURE_OMA_AUTO_DOWNLOAD, &feature_oma_off,
			DA_FEATURE_USER_DATA, download,
			NULL
		);
		break;

	case FEATURE_DESTINATION:
		retcode = da_start_download_with_extension(
			download->url,
			&(download->id),
			DA_FEATURE_OMA_AUTO_DOWNLOAD, &feature_oma_off,
			DA_FEATURE_USER_DATA, download,
			DA_FEATURE_INSTALL_PATH, download->destination,
			NULL
		);
		break;

	case FEATURE_HTTP:
		retcode = da_start_download_with_extension(
			download->url,
			&(download->id),
			DA_FEATURE_OMA_AUTO_DOWNLOAD, &feature_oma_off,
			DA_FEATURE_USER_DATA, download,
			DA_FEATURE_REQUEST_HEADER, http_headers, &http_headers_length,
			NULL
		);
		break;

	case FEATURE_DESTINATION_HTTP:
		retcode = da_start_download_with_extension(
			download->url,
			&(download->id),
			DA_FEATURE_OMA_AUTO_DOWNLOAD, &feature_oma_off,
			DA_FEATURE_USER_DATA, download,
			DA_FEATURE_INSTALL_PATH, download->destination,
			DA_FEATURE_REQUEST_HEADER, http_headers, &http_headers_length,
			NULL
		);
		break;

	default:
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_IO_ERROR, "Invalid feature reqeust");
	}

	LOGI_IF(DBG_DOWNLOAD, "[%s] id(%d) feature(%d) da-retcode(%d)", __FUNCTION__, download->id, feature_set, retcode);

	if (retcode)
	{
		return url_download_error(__FUNCTION__, url_download_agent_error(retcode), "failed to start the download");		
	}
	else
	{
		return URL_DOWNLOAD_ERROR_NONE;
	}
}


static int url_download_start_resume(url_download_h download)
{
	if (download == NULL)
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);
	}

	if (da_resume_download(download->id))
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_IO_ERROR, "failed to resume the download");
	}

	return URL_DOWNLOAD_ERROR_NONE;
}


int url_download_start(url_download_h download)
{
	if (download == NULL)
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);
	}

	switch (download->state)
	{
	case URL_DOWNLOAD_STATE_COMPLETED:
	case URL_DOWNLOAD_STATE_READY:
		return url_download_start_download(download);

	case URL_DOWNLOAD_STATE_DOWNLOADING:
		return url_download_error_invalid_state(__FUNCTION__, download);

	case URL_DOWNLOAD_STATE_PAUSED:
		return url_download_start_resume(download);

	default:
		return url_download_error_invalid_state(__FUNCTION__, download);
	}
}

int url_download_pause(url_download_h download)
{
	if (download == NULL)
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);
	}

	if (download->state != URL_DOWNLOAD_STATE_DOWNLOADING)
	{
		return url_download_error_invalid_state(__FUNCTION__, download);
	}

	if (da_suspend_download(download->id))
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_IO_ERROR, "failed to pause the download");
	}

	return URL_DOWNLOAD_ERROR_NONE;
}


int url_download_stop(url_download_h download)
{
	if (download == NULL)
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);
	}
/*
	if (download->state != URL_DOWNLOAD_STATE_DOWNLOADING)
	{
		return url_download_error_invalid_state(__FUNCTION__, download);
	}
*/
	if (da_cancel_download(download->id))
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_IO_ERROR, "failed to stop the download");
	}

	return URL_DOWNLOAD_ERROR_NONE;
}

int url_download_get_state(url_download_h download, url_download_state_e *state)
{
	if (download == NULL || state == NULL)
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);
	}

	*state = download->state;

	return URL_DOWNLOAD_ERROR_NONE;
}


typedef struct {
	url_download_h download;
	url_download_http_header_field_cb callback;
	void* user_data;
	bool foreach_break;
} foreach_context_http_header_field_t;

static void url_download_foreach_http_header_field_iterator(const char *field_name, const char *field_value, void *user_data)
{
	foreach_context_http_header_field_t *foreach_context;

	foreach_context = user_data;

	if (foreach_context == NULL)
	{
		url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);
		return;
	}


	if (foreach_context->foreach_break == true)
	{
		return;
	}

	if (foreach_context->callback != NULL)
	{
		foreach_context->foreach_break = !foreach_context->callback(foreach_context->download,
			 field_name, foreach_context->user_data);
	}

}

int url_download_foreach_http_header_field(url_download_h download, url_download_http_header_field_cb callback, void *user_data)
{
	foreach_context_http_header_field_t foreach_context = {
		.download = download,
		.callback = callback,
		.user_data = user_data,
		.foreach_break = false
	};

	if (download == NULL || callback == NULL)
	{
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);
	}

	bundle_iterate(download->http_header, url_download_foreach_http_header_field_iterator, &foreach_context);

	return URL_DOWNLOAD_ERROR_NONE;
}

