#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#include <pthread.h>
#include <signal.h>

#include <app_manager.h>

#include <dlog.h>
#include <url_download.h>
#include <url_download_private.h>
#include <download-provider.h>

#ifdef LOG_TAG
#undef LOG_TAG
#endif

#define LOG_TAG "TIZEN_N_URL_DOWNLOAD"

#define STATE_IS_RUNNING(_download_) \
	 (_download_->state == URL_DOWNLOAD_STATE_DOWNLOADING \
	 || _download_->state == URL_DOWNLOAD_STATE_PAUSED)


#define STRING_IS_INVALID(_string_) \
	(_string_ == NULL || _string_[0] == '\0')

static int url_download_resume(url_download_h download);
static int url_download_get_all_http_header_fields(
		url_download_h download, char ***fields, int *fields_length);

url_download_error_e url_download_provider_error(int error)
{
	switch (error) {
		case DOWNLOAD_ERROR_NONE:
			return URL_DOWNLOAD_ERROR_NONE;

		case DOWNLOAD_ERROR_CONNECTION_FAILED:
			return URL_DOWNLOAD_ERROR_CONNECTION_FAILED;

		case DOWNLOAD_ERROR_NETWORK_UNREACHABLE:
			return URL_DOWNLOAD_ERROR_NETWORK_UNREACHABLE;

		case DOWNLOAD_ERROR_CONNECTION_TIMED_OUT:
			return URL_DOWNLOAD_ERROR_CONNECTION_TIMED_OUT;

		case DOWNLOAD_ERROR_INVALID_DESTINATION:
			return URL_DOWNLOAD_ERROR_INVALID_DESTINATION;

		case DOWNLOAD_ERROR_NO_SPACE:
			return URL_DOWNLOAD_ERROR_NO_SPACE;

		case DOWNLOAD_ERROR_INVALID_URL:
			return URL_DOWNLOAD_ERROR_INVALID_URL;

		case DOWNLOAD_ERROR_TOO_MANY_DOWNLOADS:
			return URL_DOWNLOAD_ERROR_TOO_MANY_DOWNLOADS;

		case DOWNLOAD_ERROR_ALREADY_COMPLETED:
			return URL_DOWNLOAD_ERROR_ALREADY_COMPLETED;

		default:
			return URL_DOWNLOAD_ERROR_IO_ERROR;
	}
}

const char* url_download_error_to_string(int error_code)
{
	char *error_name = NULL;

	switch (error_code) {
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
		case URL_DOWNLOAD_ERROR_TOO_MANY_DOWNLOADS:
			error_name = "FULL_OF_MAX_DOWNLOAD_ITEMS";
			break;
		case URL_DOWNLOAD_ERROR_ALREADY_COMPLETED:
			error_name = "ALREADY_COMPLETED";
			break;
		default:
			error_name = "UNKNOWN";
			break;
	}
	return error_name;
}

int url_download_error(const char *function, int error_code, const char *description)
{
	const char *error_name = NULL;

	error_name = url_download_error_to_string(error_code);
	if (description)
		LOGE("[%s] %s(0x%08x) : %s", function, error_name, error_code, description);
	else
		LOGE("[%s] %s(0x%08x)", function, error_name, error_code);

	return error_code;
}

const char* url_download_state_to_string(url_download_state_e state)
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

int url_download_error_invalid_state(const char *function, url_download_h download)
{
	LOGE("[%s] INVALID_STATE(0x%08x) : state(%s)",
		 function, URL_DOWNLOAD_ERROR_INVALID_STATE, url_download_state_to_string(download->state));

	return URL_DOWNLOAD_ERROR_INVALID_STATE;
}

int ipc_receive_header(int fd)
{
	if(fd <= 0)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_IO_ERROR, NULL);

	download_controls msgheader = 0;
	if (read(fd, &msgheader, sizeof(download_controls)) < 0)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_IO_ERROR, NULL);

	return msgheader;
}

int ipc_send_download_control(url_download_h download, download_controls type)
{
	if (download == NULL || download->sockfd <= 0)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_IO_ERROR, NULL);

	// send control
	if (send(download->sockfd, &type, sizeof(download_controls), 0) < 0)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_IO_ERROR, NULL);

	return type;
}

// 1 thread / 1 download
void *run_receive_event_server(void *args)
{
	fd_set rset, exceptset;
	struct timeval timeout;
	download_state_info stateinfo;
	download_content_info downloadinfo;
	downloading_state_info downloadinginfo;
	download_request_state_info requeststateinfo;

	url_download_h download = (url_download_h)args;

	while(download && download->sockfd > 0) {
		FD_ZERO(&rset);
		FD_ZERO(&exceptset);
		FD_SET(download->sockfd, &rset);
		FD_SET(download->sockfd, &exceptset);
		timeout.tv_sec = 3;

		if (select((download->sockfd+1), &rset, 0, &exceptset, &timeout) < 0) {
			url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_IO_ERROR, NULL);
			if (download->callback.stopped) {
				download->callback.stopped(download,
				URL_DOWNLOAD_ERROR_IO_ERROR,
				download->callback.stopped_user_data);
			}
			break;
		}

		if (FD_ISSET(download->sockfd, &rset) > 0) {
			// read some message from socket.
			switch(ipc_receive_header(download->sockfd)) {
				case DOWNLOAD_CONTROL_GET_REQUEST_STATE_INFO :
					LOGI("[%s] DOWNLOAD_CONTROL_GET_REQUEST_STATE_INFO (started pended request)",__FUNCTION__);
					if (read(download->sockfd, &requeststateinfo, sizeof(download_request_state_info)) < 0) {
						url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_IO_ERROR, NULL);
						goto URL_DOWNLOAD_EVENT_THREAD_EXIT;
					}
					if (requeststateinfo.requestid > 0) {
						if (requeststateinfo.requestid != download->requestid)
							goto URL_DOWNLOAD_EVENT_THREAD_EXIT;
						download->requestid = requeststateinfo.requestid;
						if (requeststateinfo.stateinfo.state == DOWNLOAD_STATE_FAILED) {
							download->state = URL_DOWNLOAD_STATE_READY;
							if (download->callback.stopped) {
								download->callback.stopped(download,
								url_download_provider_error(stateinfo.err),
								download->callback.stopped_user_data);
							}
							goto URL_DOWNLOAD_EVENT_THREAD_EXIT;
						} else
							download->state = URL_DOWNLOAD_STATE_DOWNLOADING;
					} else {
						LOGE("[%s]Not Found request id (Wrong message)", __FUNCTION__);
						download->state = URL_DOWNLOAD_STATE_READY;
						if (download->callback.stopped) {
							download->callback.stopped(download,
							url_download_provider_error(stateinfo.err),
							download->callback.stopped_user_data);
						}
						goto URL_DOWNLOAD_EVENT_THREAD_EXIT;
					}
					break;
				case DOWNLOAD_CONTROL_GET_DOWNLOAD_INFO :
					if (read(download->sockfd, &downloadinfo, sizeof(download_content_info)) < 0) {
						url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_IO_ERROR, NULL);
						goto URL_DOWNLOAD_EVENT_THREAD_EXIT;
					}
					LOGI("[%s] DOWNLOAD_CONTROL_GET_DOWNLOAD_INFO [%d]%",__FUNCTION__, downloadinfo.file_size);
					download->state = URL_DOWNLOAD_STATE_DOWNLOADING;
					download->file_size = downloadinfo.file_size;
					if (downloadinfo.mime_type && strlen(downloadinfo.mime_type) > 0)
						download->mime_type = strdup(downloadinfo.mime_type);
					if (downloadinfo.content_name && strlen(downloadinfo.content_name) > 0) {
						download->content_name = strdup(downloadinfo.content_name);
						LOGI("content_name[%s] %", downloadinfo.content_name);
					}
					if (download->callback.started) {
						download->callback.started(
						download, download->content_name, download->mime_type,
						download->callback.started_user_data);
					}
					break;
				case DOWNLOAD_CONTROL_GET_DOWNLOADING_INFO :
					if (read(download->sockfd, &downloadinginfo, sizeof(downloading_state_info)) < 0) {
						url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_IO_ERROR, NULL);
						goto URL_DOWNLOAD_EVENT_THREAD_EXIT;
					}
					// call the function by download-callbacks table.
					LOGI("[%s] DOWNLOAD_CONTROL_GET_DOWNLOADING_INFO [%d]%",__FUNCTION__, downloadinginfo.received_size);
					if (download->callback.progress) {
						download->callback.progress(
						download,
						downloadinginfo.received_size, download->file_size,
						download->callback.progress_user_data);
					}
					if (downloadinginfo.saved_path &&
							strlen(downloadinginfo.saved_path) > 0) {
						LOGI("[%s] saved path [%s]",__FUNCTION__, downloadinginfo.saved_path);
						download->completed_path = strdup(downloadinginfo.saved_path);
					}
					break;
				case DOWNLOAD_CONTROL_GET_STATE_INFO :
					if (read(download->sockfd, &stateinfo, sizeof(download_state_info)) < 0) {
						url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_IO_ERROR, NULL);
						goto URL_DOWNLOAD_EVENT_THREAD_EXIT;
					}
					// call the function by download-callbacks table.
					LOGI("[%s] DOWNLOAD_CONTROL_GET_STATE_INFO state[%d]",__FUNCTION__, stateinfo.state);
					switch (stateinfo.state) {
						case DOWNLOAD_STATE_STOPPED:
							LOGI("DOWNLOAD_STATE_STOPPED");
							download->state = URL_DOWNLOAD_STATE_READY;
							if (download->callback.stopped) {
								download->callback.stopped(download,
								url_download_provider_error(stateinfo.err),
								download->callback.stopped_user_data);
							}
							// terminate this thread.
							goto URL_DOWNLOAD_EVENT_THREAD_EXIT;
							break;

						case DOWNLOAD_STATE_DOWNLOADING:
							download->state = URL_DOWNLOAD_STATE_DOWNLOADING;
							LOGI("DOWNLOAD_STATE_DOWNLOADING");
							break;
						case DOWNLOAD_STATE_PAUSE_REQUESTED:
							LOGI("DOWNLOAD_STATE_PAUSE_REQUESTED");
							break;
						case DOWNLOAD_STATE_PAUSED:
							LOGI("DOWNLOAD_STATE_PAUSED");
							download->state = URL_DOWNLOAD_STATE_PAUSED;
							if (download->callback.paused)
								download->callback.paused(download, download->callback.paused_user_data);
							break;

						case DOWNLOAD_STATE_FINISHED:
							LOGI("DOWNLOAD_STATE_FINISHED");
							download->state = URL_DOWNLOAD_STATE_COMPLETED;
							if (download->callback.completed)
								download->callback.completed(download, download->completed_path, download->callback.completed_user_data);
							// terminate this thread.
							goto URL_DOWNLOAD_EVENT_THREAD_EXIT;
							break;
						case DOWNLOAD_STATE_READY:
							LOGI("DOWNLOAD_STATE_READY");
							break;
						case DOWNLOAD_STATE_INSTALLING:
							LOGI("DOWNLOAD_STATE_INSTALLING");
							break;
						case DOWNLOAD_STATE_FAILED:
							LOGI("DOWNLOAD_STATE_FAILED");
							download->state = URL_DOWNLOAD_STATE_READY;
							if (download->callback.stopped) {
								download->callback.stopped(download,
								url_download_provider_error(stateinfo.err),
								download->callback.stopped_user_data);
							}
							goto URL_DOWNLOAD_EVENT_THREAD_EXIT;
							break;
						default:
							url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_IO_ERROR, "invalid state change event");
							if (download->callback.stopped) {
								download->state = URL_DOWNLOAD_STATE_READY;
								download->callback.stopped(download,
								URL_DOWNLOAD_ERROR_IO_ERROR,
								download->callback.stopped_user_data);
							}
							goto URL_DOWNLOAD_EVENT_THREAD_EXIT;
							break;
					}

					break;

				default :
					url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_IO_ERROR, "Invalid message");
					if (download->callback.stopped) {
						download->state = URL_DOWNLOAD_STATE_READY;
						download->callback.stopped(download,
						URL_DOWNLOAD_ERROR_IO_ERROR,
						download->callback.stopped_user_data);
					}
					goto URL_DOWNLOAD_EVENT_THREAD_EXIT;
					break;
			}
		} else if (FD_ISSET(download->sockfd, &exceptset) > 0) {
			url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_IO_ERROR, "IO Exception");
			if (download->callback.stopped) {
				download->state = URL_DOWNLOAD_STATE_READY;
				download->callback.stopped(download,
				URL_DOWNLOAD_ERROR_IO_ERROR,
				download->callback.stopped_user_data);
			}

			break;
		} else {
			// wake up by timeout. run extra job.
		}
	}
URL_DOWNLOAD_EVENT_THREAD_EXIT :
	if (download && download->sockfd) {
		FD_CLR(download->sockfd, &rset);
		FD_CLR(download->sockfd, &exceptset);
		if (download->sockfd)
			close(download->sockfd);
			download->sockfd = 0;
	}
	return NULL;
}

// fill the reqeust info.
int url_download_create(url_download_h *download)
{
	url_download_h download_new;

	if (download == NULL)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);

	download_new = (url_download_h)calloc(1, sizeof(struct url_download_s));
	if (download_new == NULL)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_OUT_OF_MEMORY, NULL);

	download_new->http_header = bundle_create();

	if (!download_new->http_header) {
		url_download_destroy(download_new);
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_IO_ERROR, "failed to initialize a bundle");
	}

	download_new->state = URL_DOWNLOAD_STATE_READY;
	download_new->sockfd = 0;
	*download = download_new;
	return URL_DOWNLOAD_ERROR_NONE;
}

int url_download_create_by_id(int id, url_download_h *download)
{
	int errorcode = URL_DOWNLOAD_ERROR_NONE;
	if (id <= 0)
		url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);
	errorcode = url_download_create(download);
	if (errorcode == URL_DOWNLOAD_ERROR_NONE)
		(*download)->requestid = id;
	return errorcode;
}

// disconnect from download-provider
int url_download_destroy(url_download_h download)
{
	if (download == NULL)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);

	if (STATE_IS_RUNNING(download))
		url_download_stop(download);

	if (download->sockfd)
		close(download->sockfd);
	download->sockfd = 0;
//	url_download_stop(download);
	if (download->url)
		free(download->url);
	if (download->destination)
		free(download->destination);
	if (download->http_header)
		free(download->http_header);
	if (download->mime_type)
		free(download->mime_type);
	if (download->content_name)
		free(download->content_name);
	if (download->completed_path)
		free(download->completed_path);
	memset(&(download->callback), 0x00, sizeof(struct url_download_cb_s));
	download->id = -1;
	free(download);

	download = NULL;
	return URL_DOWNLOAD_ERROR_NONE;
}

// connect to download-provider. then send request info.
int url_download_start(url_download_h download, int *id)
{
	struct sockaddr_un clientaddr;

	char **headers = NULL;
	int header_length = 0;

	if (!download || !download->url)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);

	if (download->state == URL_DOWNLOAD_STATE_DOWNLOADING)
		return url_download_error_invalid_state(__FUNCTION__, download);

	if (download->state == URL_DOWNLOAD_STATE_PAUSED)
		return url_download_resume(download);

	if (download->sockfd)
		close(download->sockfd);
	if ((download->sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		LOGE("[%s]socket system error : %s",__FUNCTION__,strerror(errno));
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_IO_ERROR, NULL);
	}

	bzero(&clientaddr, sizeof clientaddr);
	clientaddr.sun_family = AF_UNIX;
	strcpy(clientaddr.sun_path, DOWNLOAD_PROVIDER_IPC);
	if (connect(download->sockfd, (struct sockaddr*)&clientaddr, sizeof(clientaddr)) < 0) {
		LOGE("[%s]connect system error : %s",__FUNCTION__,strerror(errno));
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_IO_ERROR, NULL);
	}

	download_request_info requestMsg;
	memset(&requestMsg, 0x00, sizeof(download_request_info));
	requestMsg.callbackinfo.started = (download->callback.started ? 1 : 0);
	requestMsg.callbackinfo.paused = (download->callback.paused ? 1 : 0);
	requestMsg.callbackinfo.completed = (download->callback.completed ? 1 : 0);
	requestMsg.callbackinfo.stopped = (download->callback.stopped ? 1 : 0);
	requestMsg.callbackinfo.progress = (download->callback.progress ? 1 : 0);
	requestMsg.notification = download->enable_notification;

	if (download->requestid > 0)
		requestMsg.requestid = download->requestid;

	if (download->url)
		requestMsg.url.length = strlen(download->url);

	if (download->destination)
		requestMsg.install_path.length = strlen(download->destination);

	if (download->content_name)
		requestMsg.filename.length = strlen(download->content_name);

	// headers test
	if (url_download_get_all_http_header_fields(download, &headers, &header_length) !=
			URL_DOWNLOAD_ERROR_NONE) {
		return url_download_error(__FUNCTION__,
				URL_DOWNLOAD_ERROR_IO_ERROR, NULL);
	}
	if (header_length > 0) {
		int i=0;
		requestMsg.headers.rows = header_length;
		requestMsg.headers.str = (download_flexible_string*)calloc(requestMsg.headers.rows,
				sizeof(download_flexible_string));
		for(i=0; i < requestMsg.headers.rows; i++)
			requestMsg.headers.str[i].length = strlen(headers[i]);
	}

	char *app_pkgname = NULL;
	pid_t client_pid = getpid();
	int errcode = app_manager_get_package(client_pid, &app_pkgname);
	if (errcode == APP_MANAGER_ERROR_NONE)
		requestMsg.client_packagename.length = strlen(app_pkgname);
	else
		LOGE("[%s] Failed to get app_pkgname app_manager_get_package",__FUNCTION__);

	ipc_send_download_control(download, DOWNLOAD_CONTROL_START);

	if (send(download->sockfd, &requestMsg, sizeof(download_request_info), 0) < 0) {
		if (app_pkgname)
			free(app_pkgname);
		LOGE("[%s]request send system error : %s",
				__FUNCTION__, strerror(errno));
		return url_download_error(__FUNCTION__,
				URL_DOWNLOAD_ERROR_IO_ERROR, NULL);
	}
	if (requestMsg.client_packagename.length) {
		if (send(download->sockfd, app_pkgname,
				requestMsg.client_packagename.length * sizeof(char), 0) < 0) {
			if (app_pkgname)
				free(app_pkgname);
			LOGE("[%s]request send system error : %s",
					__FUNCTION__, strerror(errno));
			return url_download_error(__FUNCTION__,
					URL_DOWNLOAD_ERROR_IO_ERROR, NULL);
		}
	}
	if (app_pkgname)
		free(app_pkgname);

	if (requestMsg.url.length) {
		if (send(download->sockfd, download->url,
				requestMsg.url.length * sizeof(char), 0) < 0) {
			LOGE("[%s]request send system error : %s",
					__FUNCTION__, strerror(errno));
			return url_download_error(__FUNCTION__,
					URL_DOWNLOAD_ERROR_IO_ERROR, NULL);
		}
	}
	if (requestMsg.install_path.length) {
		if (send(download->sockfd, download->destination,
				requestMsg.install_path.length * sizeof(char), 0) < 0) {
			LOGE("[%s]request send system error : %s",
					__FUNCTION__, strerror(errno));
			return url_download_error(__FUNCTION__,
					URL_DOWNLOAD_ERROR_IO_ERROR, NULL);
		}
	}
	if (requestMsg.filename.length) {
		if (send(download->sockfd, download->content_name,
				requestMsg.filename.length * sizeof(char), 0) < 0) {
			LOGE("[%s]request send system error : %s",
					__FUNCTION__, strerror(errno));
			return url_download_error(__FUNCTION__,
					URL_DOWNLOAD_ERROR_IO_ERROR, NULL);
		}
	}

	if (requestMsg.headers.rows) {
		int i=0;
		for(i=0; i < requestMsg.headers.rows; i++) {
			if (send(download->sockfd, &requestMsg.headers.str[i],
					sizeof(download_flexible_string), 0) < 0) {
				LOGE("[%s]request send system error : %s",
						__FUNCTION__,strerror(errno));
				return url_download_error(__FUNCTION__,
						URL_DOWNLOAD_ERROR_IO_ERROR, NULL);
			}
			if (send(download->sockfd, headers[i],
					requestMsg.headers.str[i].length * sizeof(char), 0) < 0) {
				LOGE("[%s]request send system error : %s",
						__FUNCTION__,strerror(errno));
				return url_download_error(__FUNCTION__,
						URL_DOWNLOAD_ERROR_IO_ERROR, NULL);
			}
		}
		free(requestMsg.headers.str);
	}

	// Sync style
	if (ipc_receive_header(download->sockfd) == DOWNLOAD_CONTROL_GET_REQUEST_STATE_INFO) {
		download_request_state_info requeststateinfo;
		if (read(download->sockfd, &requeststateinfo, sizeof(download_request_state_info)) < 0) {
			url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_IO_ERROR, NULL);
			return -1;
		}
		if (requeststateinfo.requestid > 0) {
			download->requestid = requeststateinfo.requestid;
			(*id) = requeststateinfo.requestid;
		}
		if (requeststateinfo.stateinfo.state == DOWNLOAD_STATE_DOWNLOADING) {
			// started download normally.
			download->state = URL_DOWNLOAD_STATE_DOWNLOADING;
		}
	} else {
		url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_IO_ERROR, NULL);
		url_download_stop(download);
		return URL_DOWNLOAD_ERROR_IO_ERROR;
	}

	// capi need the thread for listening message from download-provider; this will deal the callbacks.
	if (download->callback.completed
		|| download->callback.stopped
		|| download->callback.progress
		|| download->callback.paused) {
		// check whether event thread is alive.
		if (!download->callback_thread_pid
			|| pthread_kill(download->callback_thread_pid, 0) != 0) {
			// if want to receive the events from download-provider.
			// create thread. it will listen the message through select().
			pthread_attr_t thread_attr;
			if (pthread_attr_init(&thread_attr) != 0)
				return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_OUT_OF_MEMORY, NULL);
			if (pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED) != 0) {
				return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_OUT_OF_MEMORY, NULL);
			}
			// create thread for receiving the client request.
			if (pthread_create(&download->callback_thread_pid, &thread_attr, run_receive_event_server, download) != 0)
				return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_OUT_OF_MEMORY, NULL);
		}
	}
	return URL_DOWNLOAD_ERROR_NONE;
}

// send pause message
int url_download_pause(url_download_h download)
{
	if (download == NULL || download->sockfd <= 0)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);

	if (download->state != URL_DOWNLOAD_STATE_DOWNLOADING)
		return url_download_error_invalid_state(__FUNCTION__, download);

	ipc_send_download_control(download, DOWNLOAD_CONTROL_PAUSE);

	return URL_DOWNLOAD_ERROR_NONE;
}

int url_download_resume(url_download_h download)
{
	if (download == NULL || download->sockfd <= 0)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);

	if (download->state != URL_DOWNLOAD_STATE_PAUSED)
		return url_download_error_invalid_state(__FUNCTION__, download);

	ipc_send_download_control(download, DOWNLOAD_CONTROL_RESUME);

	return URL_DOWNLOAD_ERROR_NONE;
}


// send stop message
int url_download_stop(url_download_h download)
{
	if (download == NULL || download->sockfd <= 0)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);

	if (download->state != URL_DOWNLOAD_STATE_DOWNLOADING)
		return url_download_error_invalid_state(__FUNCTION__, download);

	ipc_send_download_control(download, DOWNLOAD_CONTROL_STOP);

	return URL_DOWNLOAD_ERROR_NONE;
}

int url_download_get_state(url_download_h download, url_download_state_e *state)
{
	if (download == NULL || state == NULL)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);

	if (download->sockfd
		&& download->callback_thread_pid <= 0) {  // only when does not use the callback.

		ipc_send_download_control(download, DOWNLOAD_CONTROL_GET_STATE_INFO);

		// Sync style
		if (ipc_receive_header(download->sockfd) == DOWNLOAD_CONTROL_GET_STATE_INFO) {
			download_state_info stateinfo;
			if (read(download->sockfd, &stateinfo, sizeof(download_state_info)) < 0) {
				*state = download->state;
				return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_IO_ERROR, NULL);
			}
			download->state = url_download_provider_error(stateinfo.state);
		} else {
			*state = download->state;
			return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_IO_ERROR, NULL);
		}
	}
	*state = download->state;

	return URL_DOWNLOAD_ERROR_NONE;
}

int url_download_set_url(url_download_h download, const char *url)
{
	char *url_dup = NULL;

	if (download == NULL)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);

	if (STATE_IS_RUNNING(download))
		return url_download_error_invalid_state(__FUNCTION__, download);

	if (url != NULL) {
		url_dup = strndup(url, strlen(url));

		if (url_dup == NULL)
			return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_OUT_OF_MEMORY, NULL);
	}

	if (download->url != NULL)
		free(download->url);

	download->url = url_dup;

	return URL_DOWNLOAD_ERROR_NONE;
}


int url_download_get_url(url_download_h download, char **url)
{
	char *url_dup = NULL;

	if (download == NULL || url == NULL)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);

	if (download->url != NULL) {
		url_dup = strdup(download->url);

		if (url_dup == NULL)
			return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_OUT_OF_MEMORY, NULL);
	}

	*url = url_dup;

	return URL_DOWNLOAD_ERROR_NONE;
}


int url_download_set_destination(url_download_h download, const char *path)
{
	char *path_dup = NULL;

	if (download == NULL)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);

	if (STATE_IS_RUNNING(download))
		return url_download_error_invalid_state(__FUNCTION__, download);

	if (path != NULL) {
		path_dup = strdup(path);

		if (path_dup == NULL)
			return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_OUT_OF_MEMORY, NULL);
	}

	if (download->destination != NULL)
		free(download->destination);

	download->destination = path_dup;

	return URL_DOWNLOAD_ERROR_NONE;
}


int url_download_get_destination(url_download_h download, char **path)
{
	char *path_dup = NULL;

	if (download == NULL || path == NULL)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);

	if (download->destination != NULL) {
		path_dup = strdup(download->destination);

		if (path_dup == NULL)
			return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_OUT_OF_MEMORY, NULL);
	}

	*path = path_dup;

	return URL_DOWNLOAD_ERROR_NONE;
}

int url_download_set_file_name(url_download_h download, const char *file_name)
{
	if (download == NULL || file_name == NULL)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);

	if (download->content_name)
		free(download->content_name);
	download->content_name = strdup(file_name);
	if (download->content_name == NULL)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_OUT_OF_MEMORY, NULL);
	return URL_DOWNLOAD_ERROR_NONE;
}

int url_download_get_file_name(url_download_h download, char **file_name)
{
	char *filename_dup = NULL;

	if (download == NULL || file_name == NULL)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);

	if (download->content_name != NULL) {
		filename_dup = strdup(download->content_name);

		if (filename_dup == NULL)
			return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_OUT_OF_MEMORY, NULL);
	}

	*file_name = filename_dup;
	return URL_DOWNLOAD_ERROR_NONE;
}

int url_download_set_notification(url_download_h download, service_h service)
{
	if (download == NULL)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);
	if (service == NULL)
		download->enable_notification = 0;
	else
		download->enable_notification = 1;
	// service_h will be used later. it need to discuss more.
	return URL_DOWNLOAD_ERROR_NONE;
}

int url_download_get_notification(url_download_h download, service_h *service)
{
	int errorcode = URL_DOWNLOAD_ERROR_NONE;
	return errorcode;
}

int url_download_get_downloaded_file(url_download_h download, char **path)
{
	char *path_dup = NULL;

	if (download == NULL || path == NULL)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);

	if (download->completed_path != NULL) {
		path_dup = strdup(download->completed_path);

		if (path_dup == NULL)
			return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_OUT_OF_MEMORY, NULL);
	}

	*path = path_dup;
	return URL_DOWNLOAD_ERROR_NONE;
}

int url_download_get_mime(url_download_h download, char **mime_type)
{
	char *mime_dup = NULL;

	if (download == NULL || mime_type == NULL)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);

	if (download->mime_type != NULL) {
		mime_dup = strdup(download->mime_type);

		if (mime_dup == NULL)
			return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_OUT_OF_MEMORY, NULL);
	}

	*mime_type = mime_dup;
	return URL_DOWNLOAD_ERROR_NONE;
}

int url_download_add_http_header_field(url_download_h download, const char *field, const char *value)
{
	if (download == NULL)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);

	if (STRING_IS_INVALID(field) || STRING_IS_INVALID(value))
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);

	if (STATE_IS_RUNNING(download))
		return url_download_error_invalid_state(__FUNCTION__, download);

	if (bundle_get_val(download->http_header, field))
		bundle_del(download->http_header, field);

	if (bundle_add(download->http_header, field, value))
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_IO_ERROR, NULL);

	return URL_DOWNLOAD_ERROR_NONE;
}


int url_download_get_http_header_field(url_download_h download, const char *field, char **value)
{
	const char *bundle_value;
	char *field_value_dup;

	if (download == NULL || value == NULL)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);

	if (STRING_IS_INVALID(field))
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);

	bundle_value = bundle_get_val(download->http_header, field);

	if (bundle_value == NULL)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_FIELD_NOT_FOUND, NULL);

	field_value_dup = strdup(bundle_value);

	if (field_value_dup == NULL)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_OUT_OF_MEMORY, NULL);

	*value = field_value_dup;

	return URL_DOWNLOAD_ERROR_NONE;
}


int url_download_remove_http_header_field(url_download_h download, const char *field)
{
	if (download == NULL)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);

	if (STRING_IS_INVALID(field))
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, "invalid field");

	if (STATE_IS_RUNNING(download))
		return url_download_error_invalid_state(__FUNCTION__, download);

	if (!bundle_get_val(download->http_header, field))
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_FIELD_NOT_FOUND, NULL);

	if (bundle_del(download->http_header, field))
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_IO_ERROR, NULL);

	return URL_DOWNLOAD_ERROR_NONE;
}


int url_download_set_started_cb(url_download_h download, url_download_started_cb callback, void* user_data)
{
	if (download == NULL || callback == NULL)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);

	if (STATE_IS_RUNNING(download))
		return url_download_error_invalid_state(__FUNCTION__, download);

	download->callback.started = callback;
	download->callback.started_user_data = user_data;

	return URL_DOWNLOAD_ERROR_NONE;
}


int url_download_unset_started_cb(url_download_h download)
{
	if (download == NULL)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);

	if (STATE_IS_RUNNING(download))
//		return url_download_error_invalid_state(__FUNCTION__, download);
		url_download_error_invalid_state(__FUNCTION__, download);

	download->callback.started = NULL;
	download->callback.started_user_data = NULL;

	return URL_DOWNLOAD_ERROR_NONE;
}


int url_download_set_paused_cb(url_download_h download, url_download_paused_cb callback, void* user_data)
{
	if (download == NULL || callback == NULL)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);

	if (STATE_IS_RUNNING(download))
		return url_download_error_invalid_state(__FUNCTION__, download);

	download->callback.paused = callback;
	download->callback.paused_user_data = user_data;

	return URL_DOWNLOAD_ERROR_NONE;
}


int url_download_unset_paused_cb(url_download_h download)
{
	if (download == NULL)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);

	if (STATE_IS_RUNNING(download))
//		return url_download_error_invalid_state(__FUNCTION__, download);
		url_download_error_invalid_state(__FUNCTION__, download);

	download->callback.paused = NULL;
	download->callback.paused_user_data = NULL;

	return URL_DOWNLOAD_ERROR_NONE;
}


int url_download_set_completed_cb(url_download_h download, url_download_completed_cb callback, void* user_data)
{
	if (download == NULL || callback == NULL)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);

	if (STATE_IS_RUNNING(download))
		return url_download_error_invalid_state(__FUNCTION__, download);

	download->callback.completed = callback;
	download->callback.completed_user_data = user_data;

	return URL_DOWNLOAD_ERROR_NONE;
}


int url_download_unset_completed_cb(url_download_h download)
{
	if (download == NULL)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);

	if (STATE_IS_RUNNING(download))
//		return url_download_error_invalid_state(__FUNCTION__, download);
		url_download_error_invalid_state(__FUNCTION__, download);

	download->callback.completed = NULL;
	download->callback.completed_user_data = NULL;

	return URL_DOWNLOAD_ERROR_NONE;
}


int url_download_set_stopped_cb(url_download_h download, url_download_stopped_cb callback, void* user_data)
{
	if (download == NULL || callback == NULL)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);

	if (STATE_IS_RUNNING(download))
		return url_download_error_invalid_state(__FUNCTION__, download);

	download->callback.stopped = callback;
	download->callback.stopped_user_data = user_data;

	return URL_DOWNLOAD_ERROR_NONE;
}


int url_download_unset_stopped_cb(url_download_h download)
{
	if (download == NULL)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);

	if (STATE_IS_RUNNING(download))
//		return url_download_error_invalid_state(__FUNCTION__, download);
		url_download_error_invalid_state(__FUNCTION__, download);

	download->callback.stopped = NULL;
	download->callback.stopped_user_data = NULL;

	return URL_DOWNLOAD_ERROR_NONE;
}


int url_download_set_progress_cb(url_download_h download, url_download_progress_cb callback, void *user_data)
{
	if (download == NULL || callback == NULL)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);

	if (STATE_IS_RUNNING(download))
		return url_download_error_invalid_state(__FUNCTION__, download);

	download->callback.progress = callback;
	download->callback.progress_user_data = user_data;

	return URL_DOWNLOAD_ERROR_NONE;
}


int url_download_unset_progress_cb(url_download_h download)
{
	if (download == NULL)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);

	if (STATE_IS_RUNNING(download))
//		return url_download_error_invalid_state(__FUNCTION__, download);
		url_download_error_invalid_state(__FUNCTION__, download);

	download->callback.progress = NULL;
	download->callback.progress_user_data = NULL;

	return URL_DOWNLOAD_ERROR_NONE;
}

typedef struct http_field_array_s{
	char **array;
	int array_length;
	int position;
} http_field_array_t;

void url_download_get_all_http_header_fields_iterator(const char *field_name, const char *field_value, void *user_data)
{
	http_field_array_t *http_field_array;
	char *field_buffer;
	int field_buffer_length;
	const char *field_delimiters = ": ";

	http_field_array = user_data;

	if (http_field_array == NULL) {
		url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);
		return;
	}

	// REF : http://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.2
	field_buffer_length = strlen(field_name) + strlen(field_delimiters) + strlen(field_value) + 1;

	field_buffer = calloc(field_buffer_length, sizeof(char));

	if (field_buffer == NULL) {
		url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_OUT_OF_MEMORY, NULL);
		return;
	}

	int len = snprintf(field_buffer, field_buffer_length, "%s%s%s", field_name, field_delimiters, field_value);
	if (len == -1) {
		if (field_buffer)
			free(field_buffer);
		return;
	} else if ( len > 0 )
		field_buffer[len] = '\0';

	http_field_array->array[http_field_array->position] = field_buffer;
	http_field_array->position++;

}

int url_download_get_all_http_header_fields(url_download_h download, char ***fields, int *fields_length)
{
	http_field_array_t http_field_array;

	if (download == NULL || fields == NULL || fields_length == NULL)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);

	http_field_array.position = 0;
	http_field_array.array_length = bundle_get_count(download->http_header);
	http_field_array.array = calloc(http_field_array.array_length, sizeof(char*));

	if (http_field_array.array == NULL)
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_OUT_OF_MEMORY, NULL);

	bundle_iterate(download->http_header, url_download_get_all_http_header_fields_iterator, &http_field_array);

	*fields = http_field_array.array;
	*fields_length = http_field_array.array_length;

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

	if (foreach_context == NULL) {
		url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);
		return;
	}


	if (foreach_context->foreach_break == true)
		return;

	if (foreach_context->callback != NULL)
		foreach_context->foreach_break = !foreach_context->callback(foreach_context->download,
			 field_name, foreach_context->user_data);
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
		return url_download_error(__FUNCTION__, URL_DOWNLOAD_ERROR_INVALID_PARAMETER, NULL);

	bundle_iterate(download->http_header, url_download_foreach_http_header_field_iterator, &foreach_context);

	return URL_DOWNLOAD_ERROR_NONE;
}

