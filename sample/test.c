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
#include <string.h>
#include <Ecore.h>
#include <url_download.h>

#define LOGD(fmt, ...) \
	do { printf("[D][L:%3d] " fmt, __LINE__, ##__VA_ARGS__); \
	   printf("\n"); \
	} while(0);
#define LOGE(fmt, ...) \
	do { printf("[E][L:%3d] " fmt, __LINE__, ##__VA_ARGS__); \
	   printf("\n"); \
	} while(0);
#define LOGC(fmt, ...) \
	do { printf("[C][L:%3d] " fmt, __LINE__, ##__VA_ARGS__); \
	   printf("\n"); \
	} while(0);

#define STRINGFY(xx) #xx

#define TEST_URL "http://cdn.naver.com/naver/NanumFont/setupmac/NanumFontSetup_ECO_OTF_Ver1.0.app.zip"
#define TEST_URL2 "http://builds.nightly.webkit.org/files/trunk/src/WebKit-r109693.tar.bz2"

#define TEST_GALAXY_UA "Mozilla/5.0 (Linux; U; Android 2.3.3; ko-kr; SHW-M250S Build/GINGERBREAD) AppleWebKit/533.1 (KHTML, like Gecko) Version/4.0 Mobile Safari/533.1"
#define TEST_UA "AppleWebKit/533.1 (KHTML, like Gecko) Version/4.0 Mobile Safari/533.1"

url_download_h handle = NULL;

void print_state_str()
{
	int ret = 0;
	url_download_state_e state = 0;
	ret = url_download_get_state(handle, &state);
	if ( ret != URL_DOWNLOAD_ERROR_NONE )
	{
		LOGE("Fail to get state");
		return;
	}

	switch (state)
	{
	case URL_DOWNLOAD_STATE_READY:
		LOGD("State : Ready");
		break;
	case URL_DOWNLOAD_STATE_COMPLETED:
		LOGD("State : Competed");
		break;
	case URL_DOWNLOAD_STATE_PAUSED:
		LOGD("State : Paused");
		break;
	case URL_DOWNLOAD_STATE_DOWNLOADING:
		LOGD("State : Downloading");
		break;
	}
}

void print_error_str(url_download_error_e err)
{
	switch (err)
	{
	case URL_DOWNLOAD_ERROR_NONE:
		LOGD("%s",STRINGFY(URL_DOWNLOAD_ERROR_NONE));
		break;
	case URL_DOWNLOAD_ERROR_INVALID_PARAMETER:
		LOGD("Err:%s",STRINGFY(URL_DOWNLOAD_ERROR_INVALID_PARAMETER));
		break;
	case URL_DOWNLOAD_ERROR_OUT_OF_MEMORY:
		LOGD("Err:%s",STRINGFY(URL_DOWNLOAD_ERROR_OUT_OF_MEMORY));
		break;
	case URL_DOWNLOAD_ERROR_IO_ERROR:
		LOGD("Err:%s",STRINGFY(URL_DOWNLOAD_ERROR_IO_ERROR));
		break;
	case URL_DOWNLOAD_ERROR_NETWORK_UNREACHABLE:
		LOGD("Err:%s",STRINGFY(URL_DOWNLOAD_ERROR_NETWORK_UNREACHABLE));
		break;
	case URL_DOWNLOAD_ERROR_CONNECTION_TIMED_OUT:
		LOGD("Err:%s",STRINGFY(URL_DOWNLOAD_ERROR_CONNECTION_TIMED_OUT));
		break;
	case URL_DOWNLOAD_ERROR_NO_SPACE:
		LOGD("Err:%s", STRINGFY(URL_DOWNLOAD_ERROR_NO_SPACE));
		break;
	case URL_DOWNLOAD_ERROR_FIELD_NOT_FOUND:
		LOGD("Err:%s", STRINGFY(URL_DOWNLOAD_ERROR_FIELD_NOT_FOUND));
		break;
	case URL_DOWNLOAD_ERROR_INVALID_STATE:
		LOGD("Err:%s", STRINGFY(URL_DOWNLOAD_ERROR_INVALID_STATE));
		break;
	case URL_DOWNLOAD_ERROR_CONNECTION_FAILED:
		LOGD("Err:%s", STRINGFY(URL_DOWNLOAD_ERROR_CONNECTION_FAILED));
		break;
	case URL_DOWNLOAD_ERROR_SSL_FAILED:
		LOGD("Err:%s", STRINGFY(URL_DOWNLOAD_ERROR_SSL_FAILED));
		break;
	case URL_DOWNLOAD_ERROR_INVALID_URL:
		LOGD("Err:%s", STRINGFY(URL_DOWNLOAD_ERROR_INVALID_URL));
		break;
	default:
		LOGD("No Error");
		break;
	}
}

void completed_cb(url_download_h download, const char *path, void *user_data)
{
	LOGD("===Completed Callback===");
	print_state_str();
	if (path)
		LOGD("Downloaded Path:%s", path);
	ecore_main_loop_quit();
	LOGD("========================");
}

void progress_cb(url_download_h download, unsigned long long received, unsigned long long total,  void *user_data)
{
	LOGD("===Progress Callback===");
	print_state_str();
	LOGD("received[%llu] total[%llu]",received, total);
	LOGD("=======================");
}

void stopped_cb(url_download_h download, url_download_error_e error, void *user_data)
{
	LOGD("===Stoped Callback===");
	print_state_str();
	print_error_str(error);
	LOGD("=====================");
}

void started_cb(url_download_h download, void *user_data)
{
	LOGD("===Started Callback===");
	print_state_str();
	LOGD("======================");
}

bool http_header_cb(url_download_h download, const char *field, void *user_data)
{
	print_state_str();
	if (field)
		LOGD("HTTP header field : %s",field);
}

void init()
{
	int ret = 0;
	ret = url_download_create(&handle);
	if (ret != URL_DOWNLOAD_ERROR_NONE)
	{
		LOGE("Fail to create download handle");
		return;
	}
	ret = url_download_set_url(handle, TEST_URL);
	if ( ret != URL_DOWNLOAD_ERROR_NONE )
	{
		LOGE("Fail to set url");
		return;
	}

	ret = url_download_set_destination (handle, "./");
	if ( ret != URL_DOWNLOAD_ERROR_NONE )
	{
		LOGE("Fail to set install directory");
		return;
	}
	ret = url_download_add_http_header_field(handle, "User-Agent", TEST_UA);
	if ( ret != URL_DOWNLOAD_ERROR_NONE )
	{
		LOGE("Fail to add http header");
		return;
	}

	ret = url_download_set_completed_cb(handle, completed_cb, NULL);
	if ( ret != URL_DOWNLOAD_ERROR_NONE )
	{
		LOGE("Fail to set completed callback");
		return;
	}

	ret = url_download_set_stopped_cb(handle, stopped_cb, NULL);
	if ( ret != URL_DOWNLOAD_ERROR_NONE )
	{
		LOGE("Fail to set stopped callback");
		return;
	}

	ret = url_download_set_progress_cb(handle, progress_cb, NULL);
	if ( ret != URL_DOWNLOAD_ERROR_NONE )
	{
		LOGE("Fail to set progress callback");
		return;
	}

	ret = url_download_set_started_cb(handle, started_cb, NULL);
	if ( ret != URL_DOWNLOAD_ERROR_NONE )
	{
		LOGE("Fail to set started callback");
		return;
	}

	ret = url_download_foreach_http_header_field(handle, http_header_cb, NULL);
	if ( ret != URL_DOWNLOAD_ERROR_NONE )
	{
		LOGE("Fail to start download");
		return;
	}

	ret = url_download_start(handle);
	if ( ret != URL_DOWNLOAD_ERROR_NONE )
	{
		LOGE("Fail to start download");
		return;
	}
}

Eina_Bool request_start(void *data)
{
	int ret = 0;
	url_download_state_e state;

	LOGD("#REQUEST Start#");
	ret = url_download_get_state(handle, &state);
	if (state != URL_DOWNLOAD_STATE_PAUSED &&
		state != URL_DOWNLOAD_STATE_COMPLETED &&
		state != URL_DOWNLOAD_STATE_READY)
	{
		LOGE("Cannot stop at present state");
		return 1;
	}

	ret = url_download_start(handle);
	if ( ret != URL_DOWNLOAD_ERROR_NONE )
	{
		LOGE("Fail to start download");
	}
	return 0;
}

Eina_Bool request_stop(void *data)
{
	int ret = 0;
	url_download_state_e state;

	LOGD("#REQUEST Stop#");
	ret = url_download_get_state(handle, &state);
	if (state != URL_DOWNLOAD_STATE_PAUSED &&
		state != URL_DOWNLOAD_STATE_DOWNLOADING)
	{
		LOGC("Cannot stop at present state");
		return 1;
	}

	ret = url_download_stop(handle);
	if (ret != URL_DOWNLOAD_ERROR_NONE )
	{
		LOGE("Fail to stop download");
	}
	return 0;
}

Eina_Bool request_pause(void *data)
{
	int ret = 0;
	url_download_state_e state;

	LOGD("#REQUEST Pause#");
	ret = url_download_get_state(handle, &state);
	if (state != URL_DOWNLOAD_STATE_DOWNLOADING)
	{
		LOGC("Cannot stop at present state");
		return 1;
	}

	ret = url_download_pause(handle);
	if ( ret != URL_DOWNLOAD_ERROR_NONE )
	{
		LOGE("Fail to pause download");
	}
	return 0;
}

void deinit()
{
	int ret = 0;
	char **value = NULL;
	char **url = NULL;
	char **destination = NULL;

	ret = url_download_get_http_header_field(handle, "User-Agent", value);
	if ( ret != URL_DOWNLOAD_ERROR_NONE )
	{
		LOGE("Fail to get the value of the http field");
		return;
	}

	if (value)
	{
		int i = 0;
		int num = sizeof(value)/sizeof(char *);
		LOGD("num[%d] size[%d] size[%d]", num, sizeof(value), sizeof(char *));
		for (i = 0; i < num; i++)
		{
			LOGD("Value[%d]:%s",i+1,value[i]);
		}
	}

	ret = url_download_get_url(handle, url);
	if ( ret != URL_DOWNLOAD_ERROR_NONE )
	{
		LOGE("Fail to get url");
		return;
	}
	if (url && *url)
		LOGD("URL:%s", *url)

	ret = url_download_get_destination(handle, destination);
	if ( ret != URL_DOWNLOAD_ERROR_NONE )
	{
		LOGE("Fail to remove the value of the http field");
		return;
	}
	if (destination && *destination)
		LOGD("Detination Path:%s", *destination);


	ret = url_download_remove_http_header_field(handle, "User-Agent");
	if ( ret != URL_DOWNLOAD_ERROR_NONE )
	{
		LOGE("Fail to remove the value of the http field");
		return;
	}

	ret = url_download_unset_completed_cb(handle);
	if ( ret != URL_DOWNLOAD_ERROR_NONE )
	{
		LOGE("Fail to unset completed callback");
		return;
	}

	ret = url_download_unset_stopped_cb(handle);
	if ( ret != URL_DOWNLOAD_ERROR_NONE )
	{
		LOGE("Fail to unset stopped callback");
		return;
	}

	ret = url_download_unset_progress_cb(handle);
	if ( ret != URL_DOWNLOAD_ERROR_NONE )
	{
		LOGE("Fail to unset progress callback");
		return;
	}

	ret = url_download_unset_started_cb(handle);
	if ( ret != URL_DOWNLOAD_ERROR_NONE )
	{
		LOGE("Fail to unset started callback");
		return;
	}
	ret = url_download_destroy(handle);
	if (ret != URL_DOWNLOAD_ERROR_NONE)
	{
		LOGE("Fail to destroy download handle");
		return;
	}
}

int main()
{
	Ecore_Timer *t1 = NULL;
	Ecore_Timer *t2 = NULL;
	Ecore_Timer *t3 = NULL;
	Ecore_Timer *t4 = NULL;
	/* Should this function because libsoup use gobejct.
	 * If appcore_init() is used, g_type_init() is not needed.
	 * This is called internally in appcore_init().
	 */
	g_type_init();
	ecore_init();
	init();
	t1 = ecore_timer_add(2.0, request_stop, NULL);
	t2 = ecore_timer_add(5.0, request_start, NULL);
	t3 = ecore_timer_add(8.0, request_pause, NULL);
	t4 = ecore_timer_add(11.0, request_start, NULL);
	ecore_main_loop_begin();
	ecore_shutdown();
	LOGD("Program Exit");
}
