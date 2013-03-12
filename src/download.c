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

#include <dlog.h>
#include <download.h>
#include <download-provider.h>

#ifdef DP_ECHO_TEST
#define DOWNLOAD_ECHO_SUPPORT
#endif
#ifdef DOWNLOAD_ECHO_SUPPORT
#include <sys/ioctl.h>
#endif

#ifdef DP_DBUS_ACTIVATION
#include <dbus/dbus.h>
#endif

#define MAX_DOWNLOAD_HANDLE 5

#define DEBUG_MSG
//#define DEBUG_PRINTF

#ifdef DEBUG_MSG
#ifdef DEBUG_PRINTF
#include <stdio.h>
#define TRACE_ERROR(format, ARG...)  \
{ \
fprintf(stderr,"[URL_DOWNLOAD][%s:%d] "format"\n", __FUNCTION__, __LINE__, ##ARG); \
}
#define TRACE_STRERROR(format, ARG...)  \
{ \
fprintf(stderr,"[URL_DOWNLOAD][%s:%d] "format" [%s]\n", __FUNCTION__, __LINE__, ##ARG, strerror(errno)); \
}
#define TRACE_INFO(format, ARG...)  \
{ \
fprintf(stderr,"[URL_DOWNLOAD][%s:%d] "format"\n", __FUNCTION__, __LINE__, ##ARG); \
}
#else
#include <dlog.h>
#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "TIZEN_N_URL_DOWNLOAD"

#define TRACE_ERROR(format, ARG...)  \
{ \
LOGE(format, ##ARG); \
}
#define TRACE_STRERROR(format, ARG...)  \
{ \
LOGE(format" [%s]", ##ARG, strerror(errno)); \
}
#define TRACE_INFO(format, ARG...)  \
{ \
LOGI(format, ##ARG); \
}
#endif
#else
#define TRACE_DEBUG_MSG(format, ARG...) ;
#endif

// define type
typedef struct {
	// send command * get return value.
	int cmd_socket;
	// getting event from download-provider
	int event_socket;
	pthread_mutex_t mutex; // lock before using, unlock after using
} download_ipc_info;

typedef struct {
	download_state_changed_cb state;
	void *state_user_data;
	download_progress_cb progress;
	void *progress_user_data;
} download_cb_info;

typedef struct {
	int id;
	download_cb_info callback;
} download_slots;

typedef struct {
	int index;
	dp_event_info *event;
} download_event_info;

// declare the variables
download_ipc_info *g_download_ipc = NULL;
download_slots g_download_slots[MAX_DOWNLOAD_HANDLE];
static pthread_mutex_t g_download_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_download_event_thread_pid = 0;

#ifdef DP_DBUS_ACTIVATION
/* DBUS Activation */
static int _launch_download_provider_service(void)
{
	DBusConnection *connection = NULL;
	DBusError dbus_error;

	dbus_error_init(&dbus_error);

	connection = dbus_bus_get(DBUS_BUS_SYSTEM, &dbus_error);
	if (connection == NULL) {
		TRACE_ERROR("[ERROR] dbus_bus_get");
		if (dbus_error_is_set(&dbus_error)) {
			TRACE_ERROR("[DBUS] dbus_bus_get: %s", dbus_error.message);
			dbus_error_free(&dbus_error);
		}
		return -1;
	}

	dbus_uint32_t result = 0;
	if (dbus_bus_start_service_by_name
			(connection,
			DP_DBUS_SERVICE_DBUS, 0, &result, &dbus_error) == FALSE) {
		if (dbus_error_is_set(&dbus_error)) {
			TRACE_ERROR("[DBUS] dbus_bus_start_service_by_name: %s",
				dbus_error.message);
			dbus_error_free(&dbus_error);
		}
		dbus_connection_unref(connection);
		return -1;
	}
	if (result == DBUS_START_REPLY_ALREADY_RUNNING) {
		TRACE_INFO("DBUS_START_REPLY_ALREADY_RUNNING [%d]", result);
	} else if (result == DBUS_START_REPLY_SUCCESS) {
		TRACE_INFO("DBUS_START_REPLY_SUCCESS [%d]", result);
	}
	dbus_connection_unref(connection);
	return 0;
}
#endif

//////////// defines functions /////////////////

int _download_change_dp_errorcode(int errorcode)
{
	switch (errorcode)
	{
		case DP_ERROR_NONE :
			return DOWNLOAD_ERROR_NONE;
		case DP_ERROR_INVALID_PARAMETER :
			return DOWNLOAD_ERROR_INVALID_PARAMETER;
		case DP_ERROR_OUT_OF_MEMORY :
			return DOWNLOAD_ERROR_OUT_OF_MEMORY;
		case DP_ERROR_IO_ERROR :
			return DOWNLOAD_ERROR_IO_ERROR;
		case DP_ERROR_NETWORK_UNREACHABLE :
			return DOWNLOAD_ERROR_NETWORK_UNREACHABLE;
		case DP_ERROR_NO_SPACE :
			return DOWNLOAD_ERROR_NO_SPACE;
		case DP_ERROR_FIELD_NOT_FOUND :
			return DOWNLOAD_ERROR_FIELD_NOT_FOUND;
		case DP_ERROR_INVALID_STATE :
			return DOWNLOAD_ERROR_INVALID_STATE;
		case DP_ERROR_CONNECTION_FAILED :
			return DOWNLOAD_ERROR_CONNECTION_TIMED_OUT;
		case DP_ERROR_INVALID_URL :
			return DOWNLOAD_ERROR_INVALID_URL;
		case DP_ERROR_INVALID_DESTINATION :
			return DOWNLOAD_ERROR_INVALID_DESTINATION;
		case DP_ERROR_QUEUE_FULL :
			return DOWNLOAD_ERROR_QUEUE_FULL;
		case DP_ERROR_ALREADY_COMPLETED :
			return DOWNLOAD_ERROR_ALREADY_COMPLETED;
		case DP_ERROR_FILE_ALREADY_EXISTS :
			return DOWNLOAD_ERROR_FILE_ALREADY_EXISTS;
		case DP_ERROR_TOO_MANY_DOWNLOADS :
			return DOWNLOAD_ERROR_TOO_MANY_DOWNLOADS;
		case DP_ERROR_NO_DATA :
			return DOWNLOAD_ERROR_NO_DATA;
		case DP_ERROR_UNHANDLED_HTTP_CODE :
			return DOWNLOAD_ERROR_UNHANDLED_HTTP_CODE;
		case DP_ERROR_CANNOT_RESUME :
			return DOWNLOAD_ERROR_CANNOT_RESUME;
		case DP_ERROR_ID_NOT_FOUND :
			return DOWNLOAD_ERROR_ID_NOT_FOUND;
		case DP_ERROR_UNKNOWN :
			return DOWNLOAD_ERROR_INVALID_STATE;
		default:
		break;
	}
	return DOWNLOAD_ERROR_NONE;
}

int _download_change_dp_state(int state)
{
	switch (state)
	{
	case DP_STATE_READY:
		return DOWNLOAD_STATE_READY;
	case DP_STATE_CONNECTING:
		return DOWNLOAD_STATE_QUEUED;
	case DP_STATE_QUEUED :
		return DOWNLOAD_STATE_QUEUED;
	case DP_STATE_DOWNLOADING:
		return DOWNLOAD_STATE_DOWNLOADING;
	case DP_STATE_PAUSE_REQUESTED :
		return DOWNLOAD_STATE_DOWNLOADING;
	case DP_STATE_PAUSED :
		return DOWNLOAD_STATE_PAUSED;
	case DP_STATE_COMPLETED :
		return DOWNLOAD_STATE_COMPLETED;
	case DP_STATE_CANCELED :
		return DOWNLOAD_STATE_CANCELED;
	case DP_STATE_FAILED :
		return DOWNLOAD_STATE_FAILED;
	default:
		break;
	}
	return DOWNLOAD_ERROR_NONE;
}

void _download_print_str_state(int state)
{
	switch (state)
	{
	case DOWNLOAD_STATE_READY:
		TRACE_INFO("DOWNLOAD_STATE_READY");
		break;
	case DOWNLOAD_STATE_DOWNLOADING:
		TRACE_INFO("DOWNLOAD_STATE_DOWNLOADING");
		break;
	case DOWNLOAD_STATE_QUEUED :
		TRACE_INFO("DOWNLOAD_STATE_QUEUED");
		break;
	case DOWNLOAD_STATE_PAUSED :
		TRACE_INFO("DOWNLOAD_STATE_PAUSED");
		break;
	case DOWNLOAD_STATE_COMPLETED :
		TRACE_INFO("DOWNLOAD_STATE_COMPLETED");
		break;
	case DOWNLOAD_STATE_CANCELED :
		TRACE_INFO("DOWNLOAD_STATE_CANCELED");
		break;
	case DOWNLOAD_STATE_FAILED :
		TRACE_INFO("DOWNLOAD_STATE_FAILED");
		break;
	default:
		TRACE_INFO("Unknown state (%d)", state);
		break;
	}
}

int _get_my_slot_index(int download_id)
{
	int i = 0;
	// search same info in array.
	for (; i < MAX_DOWNLOAD_HANDLE; i++)
		if (g_download_slots[i].id == download_id)
			return i;
	return -1;
}

int _get_empty_slot_index()
{
	int i = 0;
	for (; i < MAX_DOWNLOAD_HANDLE; i++)
		if (g_download_slots[i].id <= 0)
			return i;
	return -1;
}

int _ipc_read_custom_type(int fd, void *value, size_t type_size)
{
	if (fd < 0) {
		TRACE_ERROR("[CHECK SOCKET]");
		return -1;
	}

	if (read(fd, value, type_size) < 0) {
		TRACE_STRERROR("[CRITICAL] read");
		if (errno == EPIPE) {
			TRACE_INFO("[EPIPE] Broken Pipe errno [%d]", errno);
		} else if (errno == EAGAIN) {
			TRACE_INFO("[EAGAIN] Resource temporarily unavailable errno [%d]", errno);
		} else {
			TRACE_INFO("errno [%d]", errno);
		}
		return -1;
	}
	return 0;
}

int _download_ipc_read_int(int fd)
{
	if (fd < 0) {
		TRACE_ERROR("[CHECK SOCKET]");
		return -1;
	}

	int value = -1;
	if (read(fd, &value, sizeof(int)) < 0) {
		TRACE_STRERROR("[CRITICAL] read");
		// for debugging.
		if (errno == EPIPE) {
			TRACE_INFO("[EPIPE] Broken Pipe errno [%d]", errno);
		} else if (errno == EAGAIN) {
			TRACE_INFO("[EAGAIN] Resource temporarily unavailable errno [%d]", errno);
		} else {
			TRACE_INFO("errno [%d]", errno);
		}
		return -1;
	}

	TRACE_INFO(" %d", value);
	return value;
}

// keep the order/ unsigned , str
char* _ipc_read_string(int fd)
{
	unsigned length = 0;
	char *str = NULL;

	if (fd < 0) {
		TRACE_ERROR("[CHECK FD]");
		return NULL;
	}

	// read flexible URL from client.
	if (read(fd, &length, sizeof(unsigned)) < 0) {
		TRACE_STRERROR("failed to read length [%d]", length);
		if (errno == EPIPE) {
			TRACE_INFO("[EPIPE] Broken Pipe errno [%d]", errno);
		} else if (errno == EAGAIN) {
			TRACE_INFO("[EAGAIN] Resource temporarily unavailable errno [%d]", errno);
		} else {
			TRACE_INFO("errno [%d]", errno);
		}
		return NULL;
	}
	if (length < 1 || length > DP_MAX_STR_LEN) {
		TRACE_ERROR("[STRING LEGNTH] [%d]", length);
		return NULL;
	}
	str = (char *)calloc((length + 1), sizeof(char));
	if (!str) {
		TRACE_STRERROR("[CRITICAL] allocation");
		return NULL;
	}
	if (read(fd, str, length * sizeof(char)) < 0) {
		TRACE_STRERROR("failed to read string");
		free(str);
		str = NULL;
		if (errno == EPIPE) {
			TRACE_INFO("[EPIPE] Broken Pipe errno [%d]", errno);
		} else if (errno == EAGAIN) {
			TRACE_INFO("[EAGAIN] Resource temporarily unavailable errno [%d]", errno);
		} else {
			TRACE_INFO("errno [%d]", errno);
		}
		return NULL;
	}
	str[length] = '\0';
	return str;
}

download_error_e _download_ipc_read_return(int fd)
{
	download_error_e errorcode = DOWNLOAD_ERROR_NONE;
	dp_error_type error_info = DP_ERROR_NONE;

	if (fd < 0) {
		TRACE_ERROR("[CHECK SOCKET]");
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	if (read(fd, &error_info, sizeof(dp_error_type)) < 0) {
		TRACE_STRERROR("[CRITICAL] read");
		// maybe, download-provider was crashed.
		// for debugging.
		if (errno == EPIPE) {
			TRACE_INFO("[EPIPE] Broken Pipe errno [%d]", errno);
		} else if (errno == EAGAIN) {
			TRACE_INFO("[EAGAIN] Resource temporarily unavailable errno [%d]", errno);
		} else {
			TRACE_INFO("errno [%d]", errno);
		}
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	errorcode = _download_change_dp_errorcode(error_info);
	TRACE_INFO("return : %d", errorcode);
	return errorcode;
}

dp_event_info* _download_ipc_read_event(int fd)
{
	if (fd < 0) {
		TRACE_ERROR("[CHECK SOCKET]");
		return NULL;
	}

	dp_event_info *event =
		(dp_event_info *) calloc(1, sizeof(dp_event_info));
	if (event == NULL) {
		TRACE_ERROR("[CHECK ALLOCATION]");
		return NULL;
	}
	if (read(fd, event, sizeof(dp_event_info)) < 0) {
		TRACE_STRERROR("[CRITICAL] read");
		free(event);
		if (errno == EPIPE) {
			TRACE_INFO("[EPIPE] Broken Pipe errno [%d]", errno);
		} else if (errno == EAGAIN) {
			TRACE_INFO("[EAGAIN] Resource temporarily unavailable errno [%d]", errno);
		} else {
			TRACE_INFO("errno [%d]", errno);
		}
		return NULL;
	}

	TRACE_INFO("EVENT INFO (ID : %d state : %d error : %d)",
		event->id, event->state, event->err);
	return event;
}

int _ipc_send_int(int fd, int value)
{
	if (fd < 0) {
		TRACE_ERROR("[CHECK FD] [%d]", fd);
		return -1;
	}

	if (fd >= 0 && write(fd, &value, sizeof(int)) < 0) {
		TRACE_STRERROR("send");
		if (errno == EPIPE) {
			TRACE_INFO("[EPIPE] Broken Pipe errno [%d]", errno);
		} else if (errno == EAGAIN) {
			TRACE_INFO("[EAGAIN] Resource temporarily unavailable errno [%d]", errno);
		} else {
			TRACE_INFO("errno [%d]", errno);
		}
		return -1;
	}
	return 0;
}

// keep the order/ unsigned , str
int _ipc_send_string(int fd, const char *str)
{
	unsigned length = 0;

	if (fd < 0) {
		TRACE_ERROR("[CHECK FD]");
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	if (!str) {
		TRACE_ERROR("[CHECK STRING]");
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}

	length = strlen(str);
	if (length < 1) {
		TRACE_ERROR("[CHECK LENGTH]");
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	if (fd >= 0 && write(fd, &length, sizeof(unsigned)) < 0) {
		TRACE_STRERROR("send");
		if (errno == EPIPE) {
			TRACE_INFO("[EPIPE] Broken Pipe errno [%d]", errno);
		} else if (errno == EAGAIN) {
			TRACE_INFO("[EAGAIN] Resource temporarily unavailable errno [%d]", errno);
		} else {
			TRACE_INFO("errno [%d]", errno);
		}
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	if (fd >= 0 && write(fd, str, length * sizeof(char)) < 0) {
		TRACE_STRERROR("send");
		if (errno == EPIPE) {
			TRACE_INFO("[EPIPE] Broken Pipe errno [%d]", errno);
		} else if (errno == EAGAIN) {
			TRACE_INFO("[EAGAIN] Resource temporarily unavailable errno [%d]", errno);
		} else {
			TRACE_INFO("errno [%d]", errno);
		}
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	return DOWNLOAD_ERROR_NONE;
}

int _download_ipc_send_command(int fd, int id, dp_command_type cmd)
{
	if (fd < 0) {
		TRACE_ERROR("[CHECK SOCKET]");
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	dp_command command;
	command.id = id;
	command.cmd = cmd;
	if (fd >= 0 && write(fd, &command, sizeof(dp_command)) < 0) {
		TRACE_STRERROR("[CRITICAL] send");
		if (errno == EPIPE) {
			TRACE_INFO("[EPIPE] Broken Pipe errno [%d]", errno);
		} else if (errno == EAGAIN) {
			TRACE_INFO("[EAGAIN] Resource temporarily unavailable errno [%d]", errno);
		} else {
			TRACE_INFO("errno [%d]", errno);
		}
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	return DOWNLOAD_ERROR_NONE;
}

download_error_e _send_simple_cmd(int id, dp_command_type cmd )
{
	TRACE_INFO("");

	if (cmd <= DP_CMD_NONE) {
		TRACE_ERROR("[CHECK COMMAND] (%d)", cmd);
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}
	// send commnad with ID
	if (_download_ipc_send_command
		(g_download_ipc->cmd_socket, id, cmd) != DOWNLOAD_ERROR_NONE) {
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	// return from provider.
	download_error_e errorcode =
		_download_ipc_read_return(g_download_ipc->cmd_socket);
	return errorcode;
}

int __create_socket()
{
	TRACE_INFO("");
	int sockfd = -1;
	struct timeval tv_timeo = { 2, 500000 }; //2.5 second

	struct sockaddr_un clientaddr;
	if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		TRACE_STRERROR("[CRITICAL] socket system error");
		return -1;
	}

	if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv_timeo,
			sizeof( tv_timeo ) ) < 0) {
		TRACE_STRERROR("[CRITICAL] setsockopt SO_SNDTIMEO");
		close(sockfd);
		return -1;
	}
	if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv_timeo,
			sizeof( tv_timeo ) ) < 0) {
		TRACE_STRERROR("[CRITICAL] setsockopt SO_SNDTIMEO");
		close(sockfd);
		return -1;
	}

	bzero(&clientaddr, sizeof clientaddr);
	clientaddr.sun_family = AF_UNIX;
	memset(clientaddr.sun_path, 0x00, sizeof(clientaddr.sun_path));
	strncpy(clientaddr.sun_path, DP_IPC, strlen(DP_IPC));
	clientaddr.sun_path[strlen(DP_IPC)] = '\0';
	if (connect(sockfd,
		(struct sockaddr*)&clientaddr, sizeof(clientaddr)) < 0) {
		close(sockfd);
		return -1;
	}
	TRACE_INFO("sockfd [%d]", sockfd);
	return sockfd;
}

int _disconnect_from_provider()
{
	TRACE_INFO("");
	if (g_download_ipc != NULL) {
		shutdown(g_download_ipc->cmd_socket, 0);
		close(g_download_ipc->cmd_socket);
		g_download_ipc->cmd_socket= -1;
		shutdown(g_download_ipc->event_socket, 0);
		close(g_download_ipc->event_socket);
		g_download_ipc->event_socket = -1;
		pthread_mutex_destroy((&g_download_ipc->mutex));
		free(g_download_ipc);
		g_download_ipc = NULL;

		if (g_download_event_thread_pid > 0) {
			TRACE_INFO("STOP event thread");
			pthread_cancel(g_download_event_thread_pid);
			g_download_event_thread_pid = 0;
			TRACE_INFO("OK terminate event thread");
		}
	}
	return DOWNLOAD_ERROR_NONE;
}

#ifdef DOWNLOAD_ECHO_SUPPORT
// clear read buffer. call in head of API before calling IPC_SEND
void __clear_read_buffer(int fd)
{
	long i;
	long unread_count;
	char tmp_char;

	// FIONREAD : Returns the number of bytes immediately readable
	if (ioctl(fd, FIONREAD, &unread_count) >= 0) {
		if (unread_count > 0) {
			TRACE_INFO("[CLEAN] garbage packet[%ld]", unread_count);
			for ( i = 0; i < unread_count; i++) {
				if (read(fd, &tmp_char, sizeof(char)) < 0) {
					TRACE_STRERROR("[CHECK] read");
					break;
				}
			}
		}
	}
}

// ask to provider before sending a command.
// if provider wait in some commnad, can not response immediately
// capi will wait in read block.
// after asking, call clear_read_buffer.


int _check_ipc_status(int fd)
{
	// echo from provider
	download_error_e errorcode = _send_simple_cmd(-1, DP_CMD_ECHO);
	if (errorcode != DOWNLOAD_ERROR_NONE) {
		TRACE_INFO("[ECHO] [%d]", errorcode);
		if (errorcode == DOWNLOAD_ERROR_IO_ERROR) {
			if (errno == EAGAIN) {
				TRACE_INFO("[EAGAIN] provider is busy");
			} else {
				TRACE_INFO("[CRITICAL] Broken Socket");
				_disconnect_from_provider();
			}
			return -1;
		}
	}
	__clear_read_buffer(fd);
	return 0;
}
#endif

// listen ASYNC state event, no timeout
void *_download_event_manager(void *arg)
{
	int maxfd, index;
	fd_set rset, read_fdset;
	dp_event_info *eventinfo = NULL;

	if (g_download_ipc == NULL || g_download_ipc->event_socket < 0) {
		TRACE_STRERROR("[CRITICAL] IPC NOT ESTABILISH");
		return 0;
	}

	// deferred wait to cencal until next function called.
	// ex) function : select, read in this thread
	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

	TRACE_INFO("FD [%d]", g_download_ipc->event_socket);

	maxfd = g_download_ipc->event_socket;
	FD_ZERO(&read_fdset);
	FD_SET(g_download_ipc->event_socket, &read_fdset);

	while(g_download_ipc != NULL
		&& g_download_ipc->event_socket >= 0) {
		rset = read_fdset;
		if (select((maxfd + 1), &rset, 0, 0, 0) < 0) {
			TRACE_STRERROR("[CRITICAL] select");
			break;
		}

		if (g_download_event_thread_pid <=0
			|| pthread_self() != g_download_event_thread_pid) {
			TRACE_ERROR
				("[CRITICAL] [CHECK TID] SELF ID [%d] Global ID (%d)",
				pthread_self(), g_download_event_thread_pid);
			// another thread may work. just terminate
			return 0;
		}

		pthread_mutex_lock(&g_download_mutex);
		if (g_download_ipc == NULL
			|| g_download_ipc->event_socket < 0) {
			TRACE_ERROR("[CRITICAL] IPC BROKEN Ending Event Thread");
			pthread_mutex_unlock(&g_download_mutex);
			// disconnected by main thread. just terminate
			return 0;
		}

		if (FD_ISSET(g_download_ipc->event_socket, &rset) > 0) {
			// read state info from socket
			eventinfo =
				_download_ipc_read_event(g_download_ipc->event_socket);
			if (!eventinfo || eventinfo->id <= 0) {
				// failed to read from socket // ignore this status
				if (eventinfo)
					free(eventinfo);
				TRACE_STRERROR("[CRITICAL] Can not read Event packet");
				pthread_mutex_unlock(&g_download_mutex);
				if (errno != EAGAIN) // if not timeout. end thread
					break;
				continue;
			}
			index = _get_my_slot_index(eventinfo->id);
			if (index < 0) {
				TRACE_ERROR
					("[CRITICAL] not found slot for [%d]",
				eventinfo->id);
				free(eventinfo);
				pthread_mutex_unlock(&g_download_mutex);
				continue;
			}

			pthread_mutex_unlock(&g_download_mutex);

			// begin protect callback sections
			pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

			if (eventinfo->state == DP_STATE_DOWNLOADING
				&& eventinfo->received_size > 0) {
				if (g_download_slots[index].callback.progress) {
					// progress event
					TRACE_INFO("ID %d progress callback %p",
						eventinfo->id,
						g_download_slots[index].callback.progress );
					g_download_slots[index].callback.progress
					(eventinfo->id, eventinfo->received_size,
					g_download_slots[index].callback.progress_user_data);
				}
			} else {
				if (g_download_slots[index].callback.state) {
					// state event
					TRACE_INFO("ID %d state callback %p",
						eventinfo->id,
						g_download_slots[index].callback.state );
					g_download_slots[index].callback.state
					(eventinfo->id,
					_download_change_dp_state(eventinfo->state),
					g_download_slots[index].callback.state_user_data);
				}
			}
			free(eventinfo);

			// end protect callback sections
			pthread_setcancelstate (PTHREAD_CANCEL_ENABLE,  NULL);
			continue;
		}
		pthread_mutex_unlock(&g_download_mutex);
	} // while

	FD_ZERO(&read_fdset);

	TRACE_INFO("Terminate Event Thread");
	pthread_mutex_lock(&g_download_mutex);
	TRACE_INFO("Disconnect All Connection");
	g_download_event_thread_pid = 0; // set 0 to not call pthread_cancel
	_disconnect_from_provider();
	pthread_mutex_unlock(&g_download_mutex);
	return 0;
}


int _connect_to_provider()
{
	TRACE_INFO("");
	if (g_download_ipc == NULL) {
		#ifdef DP_DBUS_ACTIVATION
		if (_launch_download_provider_service() < 0) {
			TRACE_ERROR("[DBUS IO] _launch_download_provider_service");
			return DOWNLOAD_ERROR_IO_ERROR;
		}
		#endif
		g_download_ipc =
		(download_ipc_info *) calloc(1, sizeof(download_ipc_info));
	}

	if (g_download_ipc != NULL) {

		int connect_retry = 3;
		g_download_ipc->cmd_socket = -1;
		while(g_download_ipc->cmd_socket < 0 && connect_retry-- > 0) {
			g_download_ipc->cmd_socket = __create_socket();
			if (g_download_ipc->cmd_socket < 0)
				usleep(50000);
		}
		if (g_download_ipc->cmd_socket < 0) {
			TRACE_STRERROR("[CRITICAL] connect system error");
			free(g_download_ipc);
			g_download_ipc = NULL;
			return DOWNLOAD_ERROR_IO_ERROR;
		}
		// send a command
		if (_ipc_send_int(g_download_ipc->cmd_socket,
			DP_CMD_SET_COMMAND_SOCKET) < 0) {
			close(g_download_ipc->cmd_socket);
			free(g_download_ipc);
			g_download_ipc = NULL;
			return DOWNLOAD_ERROR_IO_ERROR;
		}
		#ifndef SO_PEERCRED
		// send PID. Not support SO_PEERCRED
		if (_ipc_send_int(g_download_ipc->cmd_socket, get_pid()) < 0) {
			close(g_download_ipc->cmd_socket);
			free(g_download_ipc);
			g_download_ipc = NULL;
			return DOWNLOAD_ERROR_IO_ERROR;
		}
		#endif
		g_download_ipc->event_socket = __create_socket();
		if (g_download_ipc->event_socket < 0) {
			TRACE_STRERROR("[CRITICAL] connect system error");
			close(g_download_ipc->cmd_socket);
			free(g_download_ipc);
			g_download_ipc = NULL;
			return DOWNLOAD_ERROR_IO_ERROR;
		}
		// send a command
		if (_ipc_send_int(g_download_ipc->event_socket,
			DP_CMD_SET_EVENT_SOCKET) < 0) {
			close(g_download_ipc->cmd_socket);
			close(g_download_ipc->event_socket);
			free(g_download_ipc);
			g_download_ipc = NULL;
			return DOWNLOAD_ERROR_IO_ERROR;
		}
		#ifndef SO_PEERCRED
		// send PID. Not support SO_PEERCRED
		if (_ipc_send_int
				(g_download_ipc->event_socket, get_pid()) < 0) {
			close(g_download_ipc->cmd_socket);
			close(g_download_ipc->event_socket);
			free(g_download_ipc);
			g_download_ipc = NULL;
			return DOWNLOAD_ERROR_IO_ERROR;
		}
		#endif

		int ret = pthread_mutex_init((&g_download_ipc->mutex), NULL);
		if (ret != 0) {
			TRACE_STRERROR("ERR:pthread_mutex_init FAIL with %d.", ret);
			_disconnect_from_provider();
			return DOWNLOAD_ERROR_IO_ERROR;
		}

		if (g_download_event_thread_pid <= 0) {
			// create thread here ( getting event_socket )
			pthread_attr_t thread_attr;
			if (pthread_attr_init(&thread_attr) != 0) {
				TRACE_STRERROR("[CRITICAL] pthread_attr_init");
				_disconnect_from_provider();
				return DOWNLOAD_ERROR_IO_ERROR;
			}
			if (pthread_attr_setdetachstate(&thread_attr,
				PTHREAD_CREATE_DETACHED) != 0) {
				TRACE_STRERROR("[CRITICAL] pthread_attr_setdetachstate");
				_disconnect_from_provider();
				return DOWNLOAD_ERROR_IO_ERROR;
			}
			if (pthread_create(&g_download_event_thread_pid,
							&thread_attr,
							_download_event_manager,
							g_download_ipc) != 0) {
				TRACE_STRERROR("[CRITICAL] pthread_create");
				_disconnect_from_provider();
				return DOWNLOAD_ERROR_IO_ERROR;
			}
		}
	}
	return DOWNLOAD_ERROR_NONE;
}

download_error_e _check_connections()
{
	int ret = 0;
	if (g_download_ipc == NULL)
		if ((ret = _connect_to_provider()) != DOWNLOAD_ERROR_NONE)
			return ret;
	if (g_download_ipc == NULL || g_download_ipc->cmd_socket < 0) {
		TRACE_ERROR("[CHECK IPC]");
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	return DOWNLOAD_ERROR_NONE;
}



/////////////////////// APIs /////////////////////////////////

int download_create(int *download_id)
{
	int errorcode = DOWNLOAD_ERROR_NONE;
	int t_download_id = 0;

	pthread_mutex_lock(&g_download_mutex);
	TRACE_INFO("");

	int index = _get_empty_slot_index();
	if (index < 0) {
		TRACE_ERROR
			("[ERROR] TOO_MANY_DOWNLOADS[%d]", MAX_DOWNLOAD_HANDLE);
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_TOO_MANY_DOWNLOADS;
	}

	if (_check_connections() != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	pthread_mutex_lock((&g_download_ipc->mutex));

#ifdef DOWNLOAD_ECHO_SUPPORT
	if (_check_ipc_status(g_download_ipc->cmd_socket) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
#endif

	errorcode = _send_simple_cmd(-1, DP_CMD_CREATE);
	if (errorcode != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errorcode == DOWNLOAD_ERROR_IO_ERROR && errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return errorcode;
	}

	// getting state with ID from provider.
	t_download_id = _download_ipc_read_int(g_download_ipc->cmd_socket);
	if (t_download_id < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	TRACE_INFO("ID : %d", t_download_id);

	*download_id = t_download_id;
	g_download_slots[index].id = t_download_id;
	pthread_mutex_unlock((&g_download_ipc->mutex));

	pthread_mutex_unlock(&g_download_mutex);
	return DOWNLOAD_ERROR_NONE;
}

int download_destroy(int download_id)
{
	int index = -1;
	int errorcode = DOWNLOAD_ERROR_NONE;

	if (download_id <= 0) {
		TRACE_ERROR("[CHECK ID] (%d)", download_id);
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}

	pthread_mutex_lock(&g_download_mutex);
	TRACE_INFO("");

	if (_check_connections() != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	pthread_mutex_lock((&g_download_ipc->mutex));

#ifdef DOWNLOAD_ECHO_SUPPORT
	if (_check_ipc_status(g_download_ipc->cmd_socket) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
#endif

	index = _get_my_slot_index(download_id);
	if (index >= 0) {
		g_download_slots[index].id = 0;
		g_download_slots[index].callback.state = NULL;
		g_download_slots[index].callback.state_user_data = NULL;
		g_download_slots[index].callback.progress = NULL;
		g_download_slots[index].callback.progress_user_data = NULL;
	}
	errorcode = _send_simple_cmd(download_id, DP_CMD_DESTROY);
	if (errorcode != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errorcode == DOWNLOAD_ERROR_IO_ERROR && errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return errorcode;
	}

	// after getting errorcode, send FREE to provider.
	TRACE_INFO("Request to Free the memory for ID : %d", download_id);
	// send again DP_CMD_FREE with ID.
	errorcode = _download_ipc_send_command
		(g_download_ipc->cmd_socket, download_id, DP_CMD_FREE);
	if (errorcode != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return errorcode;
	}

	pthread_mutex_unlock((&g_download_ipc->mutex));
	pthread_mutex_unlock(&g_download_mutex);
	return DOWNLOAD_ERROR_NONE;
}

int download_start(int download_id)
{
	int errorcode = DOWNLOAD_ERROR_NONE;

	if (download_id <= 0) {
		TRACE_ERROR("[CHECK ID] (%d)", download_id);
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}

	pthread_mutex_lock(&g_download_mutex);
	TRACE_INFO("");

	if (_check_connections() != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	pthread_mutex_lock((&g_download_ipc->mutex));

#ifdef DOWNLOAD_ECHO_SUPPORT
	if (_check_ipc_status(g_download_ipc->cmd_socket) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
#endif

	errorcode = _send_simple_cmd(download_id, DP_CMD_START);
	if (errorcode != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errorcode == DOWNLOAD_ERROR_IO_ERROR && errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return errorcode;
	}
	pthread_mutex_unlock((&g_download_ipc->mutex));
	pthread_mutex_unlock(&g_download_mutex);
	return DOWNLOAD_ERROR_NONE;
}

int download_pause(int download_id)
{
	int errorcode = DOWNLOAD_ERROR_NONE;

	if (download_id <= 0) {
		TRACE_ERROR("[CHECK ID] (%d)", download_id);
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}

	pthread_mutex_lock(&g_download_mutex);
	TRACE_INFO("");

	if (_check_connections() != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	pthread_mutex_lock((&g_download_ipc->mutex));

#ifdef DOWNLOAD_ECHO_SUPPORT
	if (_check_ipc_status(g_download_ipc->cmd_socket) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
#endif

	errorcode = _send_simple_cmd(download_id, DP_CMD_PAUSE);
	if (errorcode != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errorcode == DOWNLOAD_ERROR_IO_ERROR && errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return errorcode;
	}
	pthread_mutex_unlock((&g_download_ipc->mutex));
	pthread_mutex_unlock(&g_download_mutex);
	return DOWNLOAD_ERROR_NONE;
}

int download_cancel(int download_id)
{
	int errorcode = DOWNLOAD_ERROR_NONE;

	if (download_id <= 0) {
		TRACE_ERROR("[CHECK ID] (%d)", download_id);
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}

	pthread_mutex_lock(&g_download_mutex);
	TRACE_INFO("");

	if (_check_connections() != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	pthread_mutex_lock((&g_download_ipc->mutex));

#ifdef DOWNLOAD_ECHO_SUPPORT
	if (_check_ipc_status(g_download_ipc->cmd_socket) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
#endif

	errorcode = _send_simple_cmd(download_id, DP_CMD_CANCEL);
	if (errorcode != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errorcode == DOWNLOAD_ERROR_IO_ERROR && errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return errorcode;
	}
	pthread_mutex_unlock((&g_download_ipc->mutex));
	pthread_mutex_unlock(&g_download_mutex);
	return DOWNLOAD_ERROR_NONE;
}


int download_set_url(int download_id, const char *url)
{
	if (download_id <= 0) {
		TRACE_ERROR("[CHECK ID] (%d)", download_id);
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}
	if (!url) {
		TRACE_ERROR("[CHECK url]");
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}

	TRACE_INFO("");
	pthread_mutex_lock(&g_download_mutex);

	if (_check_connections() != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	pthread_mutex_lock((&g_download_ipc->mutex));

#ifdef DOWNLOAD_ECHO_SUPPORT
	if (_check_ipc_status(g_download_ipc->cmd_socket) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
#endif

	// send commnad with ID
	if (_download_ipc_send_command
		(g_download_ipc->cmd_socket, download_id, DP_CMD_SET_URL)
		!= DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	if (_ipc_send_string(g_download_ipc->cmd_socket, url)
		!= DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	// return from provider.
	download_error_e errorcode =
		_download_ipc_read_return(g_download_ipc->cmd_socket);
	if (errorcode == DOWNLOAD_ERROR_IO_ERROR) {
		TRACE_ERROR("[CHECK IO] (%d)", download_id);
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return errorcode;
	}
	pthread_mutex_unlock((&g_download_ipc->mutex));
	pthread_mutex_unlock(&g_download_mutex);
	return errorcode;
}


int download_get_url(int download_id, char **url)
{
	int errorcode = DOWNLOAD_ERROR_NONE;
	char *value = NULL;

	if (download_id <= 0) {
		TRACE_ERROR("[CHECK ID] (%d)", download_id);
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}

	pthread_mutex_lock(&g_download_mutex);
	TRACE_INFO("");

	if (_check_connections() != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	pthread_mutex_lock((&g_download_ipc->mutex));

#ifdef DOWNLOAD_ECHO_SUPPORT
	if (_check_ipc_status(g_download_ipc->cmd_socket) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
#endif

	errorcode = _send_simple_cmd(download_id, DP_CMD_GET_URL);
	if (errorcode != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errorcode == DOWNLOAD_ERROR_IO_ERROR && errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return errorcode;
	}
	// getting state with ID from provider.
	value = _ipc_read_string(g_download_ipc->cmd_socket);
	if (value == NULL) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	*url = value;
	TRACE_INFO("ID : %d url : %s", download_id, *url);
	// it need to free
	pthread_mutex_unlock((&g_download_ipc->mutex));
	pthread_mutex_unlock(&g_download_mutex);
	return DOWNLOAD_ERROR_NONE;
}

int download_set_network_type(int download_id,
						download_network_type_e net_type)
{
	int network_type = DP_NETWORK_TYPE_ALL;

	if (net_type == DOWNLOAD_NETWORK_WIFI)
		network_type = DP_NETWORK_TYPE_WIFI;
	else if (net_type == DOWNLOAD_NETWORK_DATA_NETWORK)
		network_type = DP_NETWORK_TYPE_DATA_NETWORK;

	if (download_id <= 0) {
		TRACE_ERROR("[CHECK ID] (%d)", download_id);
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}

	pthread_mutex_lock(&g_download_mutex);
	TRACE_INFO("");

	if (_check_connections() != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	pthread_mutex_lock((&g_download_ipc->mutex));

#ifdef DOWNLOAD_ECHO_SUPPORT
	if (_check_ipc_status(g_download_ipc->cmd_socket) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
#endif

	if (_download_ipc_send_command(g_download_ipc->cmd_socket,
		download_id, DP_CMD_SET_NETWORK_TYPE)
		!= DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	if (_ipc_send_int(g_download_ipc->cmd_socket, network_type) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	// return from provider.
	download_error_e errorcode =
		_download_ipc_read_return(g_download_ipc->cmd_socket);
	if (errorcode == DOWNLOAD_ERROR_IO_ERROR) {
		TRACE_ERROR("[CHECK IO] (%d)", download_id);
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return errorcode;
	}
	pthread_mutex_unlock((&g_download_ipc->mutex));
	pthread_mutex_unlock(&g_download_mutex);
	return errorcode;
}

int download_get_network_type(int download_id,
							download_network_type_e *net_type)
{
	int errorcode = DOWNLOAD_ERROR_NONE;
	int network_type = DP_NETWORK_TYPE_ALL;

	if (download_id <= 0) {
		TRACE_ERROR("[CHECK ID] (%d)", download_id);
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}

	pthread_mutex_lock(&g_download_mutex);
	TRACE_INFO("");

	if (_check_connections() != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	pthread_mutex_lock((&g_download_ipc->mutex));

#ifdef DOWNLOAD_ECHO_SUPPORT
	if (_check_ipc_status(g_download_ipc->cmd_socket) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
#endif

	errorcode = _send_simple_cmd(download_id, DP_CMD_GET_NETWORK_TYPE);
	if (errorcode != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errorcode == DOWNLOAD_ERROR_IO_ERROR && errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return errorcode;
	}

	network_type = _download_ipc_read_int(g_download_ipc->cmd_socket);
	if (network_type < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	if (network_type == DP_NETWORK_TYPE_WIFI)
		*net_type = DOWNLOAD_NETWORK_WIFI;
	else if (network_type == DP_NETWORK_TYPE_DATA_NETWORK)
		*net_type = DOWNLOAD_NETWORK_DATA_NETWORK;
	else
		*net_type = DOWNLOAD_NETWORK_ALL;

	TRACE_INFO("ID : %d network_type : %d", download_id, *net_type);
	pthread_mutex_unlock((&g_download_ipc->mutex));
	pthread_mutex_unlock(&g_download_mutex);
	return DOWNLOAD_ERROR_NONE;
}

int download_set_destination(int download_id, const char *path)
{
	if (download_id <= 0) {
		TRACE_ERROR("[CHECK ID] (%d)", download_id);
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}
	if (!path) {
		TRACE_ERROR("[CHECK PATH]");
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}

	pthread_mutex_lock(&g_download_mutex);
	TRACE_INFO("");

	if (_check_connections() != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	pthread_mutex_lock((&g_download_ipc->mutex));

#ifdef DOWNLOAD_ECHO_SUPPORT
	if (_check_ipc_status(g_download_ipc->cmd_socket) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
#endif

	// send commnad with ID
	if (_download_ipc_send_command
		(g_download_ipc->cmd_socket, download_id, DP_CMD_SET_DESTINATION)
		!= DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	if (_ipc_send_string(g_download_ipc->cmd_socket, path)
		!= DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	// return from provider.
	download_error_e errorcode =
		_download_ipc_read_return(g_download_ipc->cmd_socket);
	if (errorcode == DOWNLOAD_ERROR_IO_ERROR) {
		TRACE_ERROR("[CHECK IO] (%d)", download_id);
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return errorcode;
	}
	pthread_mutex_unlock((&g_download_ipc->mutex));
	pthread_mutex_unlock(&g_download_mutex);
	return errorcode;
}


int download_get_destination(int download_id, char **path)
{
	int errorcode = DOWNLOAD_ERROR_NONE;
	char *value = NULL;

	if (download_id <= 0) {
		TRACE_ERROR("[CHECK ID] (%d)", download_id);
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}

	pthread_mutex_lock(&g_download_mutex);
	TRACE_INFO("");

	if (_check_connections() != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	pthread_mutex_lock((&g_download_ipc->mutex));

#ifdef DOWNLOAD_ECHO_SUPPORT
	if (_check_ipc_status(g_download_ipc->cmd_socket) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
#endif

	errorcode =
		_send_simple_cmd(download_id, DP_CMD_GET_DESTINATION);
	if (errorcode != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errorcode == DOWNLOAD_ERROR_IO_ERROR && errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return errorcode;
	}
	// getting string with ID from provider.
	value = _ipc_read_string(g_download_ipc->cmd_socket);
	if (value == NULL) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	*path = value;
	TRACE_INFO("ID : %d path : %s", download_id, *path);
	// it need to free
	pthread_mutex_unlock((&g_download_ipc->mutex));
	pthread_mutex_unlock(&g_download_mutex);
	return DOWNLOAD_ERROR_NONE;
}

int download_set_file_name(int download_id, const char *file_name)
{
	if (download_id <= 0) {
		TRACE_ERROR("[CHECK ID] (%d)", download_id);
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}
	if (!file_name) {
		TRACE_ERROR("[CHECK file_name]");
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}

	pthread_mutex_lock(&g_download_mutex);
	TRACE_INFO("");

	if (_check_connections() != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	pthread_mutex_lock((&g_download_ipc->mutex));

#ifdef DOWNLOAD_ECHO_SUPPORT
	if (_check_ipc_status(g_download_ipc->cmd_socket) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
#endif

	// send commnad with ID
	if (_download_ipc_send_command
		(g_download_ipc->cmd_socket, download_id, DP_CMD_SET_FILENAME)
		!= DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	if (_ipc_send_string(g_download_ipc->cmd_socket, file_name)
		!= DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	// return from provider.
	download_error_e errorcode =
		_download_ipc_read_return(g_download_ipc->cmd_socket);
	if (errorcode == DOWNLOAD_ERROR_IO_ERROR) {
		TRACE_ERROR("[CHECK IO] (%d)", download_id);
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return errorcode;
	}
	pthread_mutex_unlock((&g_download_ipc->mutex));
	pthread_mutex_unlock(&g_download_mutex);
	return errorcode;
}

int download_get_file_name(int download_id, char **file_name)
{
	int errorcode = DOWNLOAD_ERROR_NONE;
	char *value = NULL;

	if (download_id <= 0) {
		TRACE_ERROR("[CHECK ID] (%d)", download_id);
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}

	pthread_mutex_lock(&g_download_mutex);
	TRACE_INFO("");

	if (_check_connections() != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	pthread_mutex_lock((&g_download_ipc->mutex));

#ifdef DOWNLOAD_ECHO_SUPPORT
	if (_check_ipc_status(g_download_ipc->cmd_socket) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
#endif

	errorcode =
		_send_simple_cmd(download_id, DP_CMD_GET_FILENAME);
	if (errorcode != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errorcode == DOWNLOAD_ERROR_IO_ERROR && errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return errorcode;
	}
	// getting string with ID from provider.
	value = _ipc_read_string(g_download_ipc->cmd_socket);
	if (value == NULL) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	*file_name = value;
	TRACE_INFO("ID : %d file_name : %s", download_id, *file_name);
	// it need to free
	pthread_mutex_unlock((&g_download_ipc->mutex));
	pthread_mutex_unlock(&g_download_mutex);
	return DOWNLOAD_ERROR_NONE;
}

int download_set_ongoing_notification(int download_id, bool enable)
{
	return download_set_notification(download_id, enable);
}

int download_set_notification(int download_id, bool enable)
{
	int value = enable;

	if (download_id <= 0) {
		TRACE_ERROR("[CHECK ID] (%d)", download_id);
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}

	pthread_mutex_lock(&g_download_mutex);
	TRACE_INFO("");

	if (_check_connections() != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	pthread_mutex_lock((&g_download_ipc->mutex));

#ifdef DOWNLOAD_ECHO_SUPPORT
	if (_check_ipc_status(g_download_ipc->cmd_socket) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
#endif

	if (_download_ipc_send_command(g_download_ipc->cmd_socket,
		download_id, DP_CMD_SET_NOTIFICATION)
		!= DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	if (_ipc_send_int(g_download_ipc->cmd_socket, value) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	// return from provider.
	download_error_e errorcode =
		_download_ipc_read_return(g_download_ipc->cmd_socket);
	if (errorcode == DOWNLOAD_ERROR_IO_ERROR) {
		TRACE_ERROR("[CHECK IO] (%d)", download_id);
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return errorcode;
	}
	pthread_mutex_unlock((&g_download_ipc->mutex));
	pthread_mutex_unlock(&g_download_mutex);
	return errorcode;

}

int download_get_ongoing_notification(int download_id, bool *enable)
{
	return download_get_notification(download_id, enable);
}

int download_get_notification(int download_id, bool *enable)
{
	int value = 0;
	int errorcode = DOWNLOAD_ERROR_NONE;
	if (download_id <= 0) {
		TRACE_ERROR("[CHECK ID] (%d)", download_id);
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}

	pthread_mutex_lock(&g_download_mutex);
	TRACE_INFO("");

	if (_check_connections() != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	pthread_mutex_lock((&g_download_ipc->mutex));

#ifdef DOWNLOAD_ECHO_SUPPORT
	if (_check_ipc_status(g_download_ipc->cmd_socket) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
#endif

	errorcode = _send_simple_cmd(download_id, DP_CMD_GET_NOTIFICATION);
	if (errorcode != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errorcode == DOWNLOAD_ERROR_IO_ERROR && errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return errorcode;
	}
	value = _download_ipc_read_int(g_download_ipc->cmd_socket);
	if (value < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	*enable = value;
	TRACE_INFO("ID : %d auto download : %d", download_id, *enable);
	pthread_mutex_unlock((&g_download_ipc->mutex));
	pthread_mutex_unlock(&g_download_mutex);
	return DOWNLOAD_ERROR_NONE;

}

int download_get_downloaded_file_path(int download_id, char **path)
{
	int errorcode = DOWNLOAD_ERROR_NONE;
	char *value = NULL;

	if (download_id <= 0) {
		TRACE_ERROR("[CHECK ID] (%d)", download_id);
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}

	pthread_mutex_lock(&g_download_mutex);
	TRACE_INFO("");

	if (_check_connections() != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	pthread_mutex_lock((&g_download_ipc->mutex));

#ifdef DOWNLOAD_ECHO_SUPPORT
	if (_check_ipc_status(g_download_ipc->cmd_socket) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
#endif

	errorcode =
		_send_simple_cmd(download_id, DP_CMD_GET_SAVED_PATH);
	if (errorcode != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errorcode == DOWNLOAD_ERROR_IO_ERROR && errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return errorcode;
	}
	// getting string with ID from provider.
	value = _ipc_read_string(g_download_ipc->cmd_socket);
	if (value == NULL) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	*path = value;
	TRACE_INFO("ID : %d path : %s", download_id, *path);
	// it need to free
	pthread_mutex_unlock((&g_download_ipc->mutex));
	pthread_mutex_unlock(&g_download_mutex);
	return DOWNLOAD_ERROR_NONE;
}

int download_set_notification_extra_param(int download_id, char *key, char *value)
{
	if (download_id <= 0) {
		TRACE_ERROR("[CHECK ID] (%d)", download_id);
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}
	if (!key || !value) {
		TRACE_ERROR("[CHECK param]");
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}

	TRACE_INFO("");
	pthread_mutex_lock(&g_download_mutex);

	if (_check_connections() != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	pthread_mutex_lock((&g_download_ipc->mutex));

#ifdef DOWNLOAD_ECHO_SUPPORT
	if (_check_ipc_status(g_download_ipc->cmd_socket) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
#endif

	// send commnad with ID
	if (_download_ipc_send_command
		(g_download_ipc->cmd_socket, download_id, DP_CMD_SET_EXTRA_PARAM)
		!= DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	if (_ipc_send_string(g_download_ipc->cmd_socket, key)
		!= DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	if (_ipc_send_string(g_download_ipc->cmd_socket, value)
		!= DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	// return from provider.
	download_error_e errorcode =
		_download_ipc_read_return(g_download_ipc->cmd_socket);
	if (errorcode == DOWNLOAD_ERROR_IO_ERROR) {
		TRACE_ERROR("[CHECK IO] (%d)", download_id);
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return errorcode;
	}
	pthread_mutex_unlock((&g_download_ipc->mutex));
	pthread_mutex_unlock(&g_download_mutex);
	return errorcode;
}

int download_get_notification_extra_param(int download_id, char **key, char **value)
{
	int errorcode = DOWNLOAD_ERROR_NONE;
	char *key_str = NULL;
	char *value_str = NULL;

	if (download_id <= 0) {
		TRACE_ERROR("[CHECK ID] (%d)", download_id);
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}

	pthread_mutex_lock(&g_download_mutex);
	TRACE_INFO("");

	if (_check_connections() != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	pthread_mutex_lock((&g_download_ipc->mutex));

#ifdef DOWNLOAD_ECHO_SUPPORT
	if (_check_ipc_status(g_download_ipc->cmd_socket) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
#endif

	errorcode = _send_simple_cmd(download_id, DP_CMD_GET_EXTRA_PARAM);
	if (errorcode != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errorcode == DOWNLOAD_ERROR_IO_ERROR && errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return errorcode;
	}
	// getting state with ID from provider.
	key_str = _ipc_read_string(g_download_ipc->cmd_socket);
	if (key_str == NULL) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	value_str = _ipc_read_string(g_download_ipc->cmd_socket);
	if (value_str == NULL) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		free(key_str);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	*key = key_str;
	*value = value_str;
	TRACE_INFO("ID : %d key : %s value : %s", download_id, *key, *value);
	// it need to free
	pthread_mutex_unlock((&g_download_ipc->mutex));
	pthread_mutex_unlock(&g_download_mutex);
	return DOWNLOAD_ERROR_NONE;
}

int download_add_http_header_field(int download_id, const char *field,
	const char *value)
{
	download_error_e errorcode = DOWNLOAD_ERROR_NONE;
	TRACE_INFO("");

	if (download_id <= 0) {
		TRACE_ERROR("[CHECK ID] (%d)", download_id);
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}
	if (!field || !value) {
		TRACE_ERROR("[CHECK field or value]");
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}

	pthread_mutex_lock(&g_download_mutex);

	if (_check_connections() != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	pthread_mutex_lock((&g_download_ipc->mutex));

#ifdef DOWNLOAD_ECHO_SUPPORT
	if (_check_ipc_status(g_download_ipc->cmd_socket) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
#endif

	// send commnad with ID
	if (_download_ipc_send_command
		(g_download_ipc->cmd_socket, download_id, DP_CMD_SET_HTTP_HEADER)
		!= DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	if (_ipc_send_string(g_download_ipc->cmd_socket, field)
		!= DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	if (_ipc_send_string(g_download_ipc->cmd_socket, value)
		!= DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	// return from provider.
	errorcode =
		_download_ipc_read_return(g_download_ipc->cmd_socket);
	if (errorcode == DOWNLOAD_ERROR_IO_ERROR) {
		TRACE_ERROR("[CHECK IO] (%d)", download_id);
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return errorcode;
	}
	pthread_mutex_unlock((&g_download_ipc->mutex));
	pthread_mutex_unlock(&g_download_mutex);
	return errorcode;
}

int download_get_http_header_field(int download_id,
	const char *field, char **value)
{
	download_error_e errorcode = DOWNLOAD_ERROR_NONE;
	char *str = NULL;

	if (download_id <= 0) {
		TRACE_ERROR("[CHECK ID] (%d)", download_id);
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}

	if (!field || !value) {
		TRACE_ERROR("[CHECK field or value]");
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}

	pthread_mutex_lock(&g_download_mutex);
	TRACE_INFO("");

	if (_check_connections() != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	pthread_mutex_lock((&g_download_ipc->mutex));

#ifdef DOWNLOAD_ECHO_SUPPORT
	if (_check_ipc_status(g_download_ipc->cmd_socket) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
#endif

	if (_download_ipc_send_command
		(g_download_ipc->cmd_socket, download_id, DP_CMD_GET_HTTP_HEADER)
		!= DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errorcode == DOWNLOAD_ERROR_IO_ERROR && errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return errorcode;
	}

	if (_ipc_send_string(g_download_ipc->cmd_socket, field)
		!= DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	errorcode =
		_download_ipc_read_return(g_download_ipc->cmd_socket);
	if (errorcode != DOWNLOAD_ERROR_NONE) {
		TRACE_ERROR("[CHECK IO] (%d)", download_id);
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return errorcode;
	}

	// getting string with ID from provider.
	str = _ipc_read_string(g_download_ipc->cmd_socket);
	if (str == NULL) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	*value = str;
	TRACE_INFO("ID : %d field:%s value: %s", download_id, field, *value);
	// it need to free
	pthread_mutex_unlock((&g_download_ipc->mutex));
	pthread_mutex_unlock(&g_download_mutex);
	return DOWNLOAD_ERROR_NONE;
}

int download_remove_http_header_field(int download_id,
	const char *field)
{
	download_error_e errorcode = DOWNLOAD_ERROR_NONE;

	TRACE_INFO("");

	if (download_id <= 0) {
		TRACE_ERROR("[CHECK ID] (%d)", download_id);
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}
	if (!field) {
		TRACE_ERROR("[CHECK field]");
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}

	pthread_mutex_lock(&g_download_mutex);

	if (_check_connections() != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	pthread_mutex_lock((&g_download_ipc->mutex));

#ifdef DOWNLOAD_ECHO_SUPPORT
	if (_check_ipc_status(g_download_ipc->cmd_socket) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
#endif

	// send commnad with ID
	if (_download_ipc_send_command
		(g_download_ipc->cmd_socket, download_id, DP_CMD_DEL_HTTP_HEADER)
		!= DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	if (_ipc_send_string(g_download_ipc->cmd_socket, field)
		!= DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	// return from provider.
	errorcode =
		_download_ipc_read_return(g_download_ipc->cmd_socket);
	if (errorcode == DOWNLOAD_ERROR_IO_ERROR) {
		TRACE_ERROR("[CHECK IO] (%d)", download_id);
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return errorcode;
	}
	pthread_mutex_unlock((&g_download_ipc->mutex));
	pthread_mutex_unlock(&g_download_mutex);

	return errorcode;
}

int download_set_state_changed_cb(int download_id,
	download_state_changed_cb callback, void* user_data)
{
	int index = -1;

	if (download_id <= 0) {
		TRACE_ERROR("[CHECK ID] (%d)", download_id);
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}
	if (callback == NULL) {
		download_unset_state_changed_cb(download_id);
		return DOWNLOAD_ERROR_NONE;
	}

	pthread_mutex_lock(&g_download_mutex);
	TRACE_INFO("");

	if (_check_connections() != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	// turn on state_cb flag of provider
	pthread_mutex_lock((&g_download_ipc->mutex));

#ifdef DOWNLOAD_ECHO_SUPPORT
	if (_check_ipc_status(g_download_ipc->cmd_socket) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
#endif
	// send create command.
	if (_download_ipc_send_command(g_download_ipc->cmd_socket,
		download_id, DP_CMD_SET_STATE_CALLBACK)
		!= DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		download_unset_state_changed_cb(download_id);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	if (_ipc_send_int(g_download_ipc->cmd_socket, 1) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		download_unset_state_changed_cb(download_id);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	// return from provider.
	download_error_e errorcode =
		_download_ipc_read_return(g_download_ipc->cmd_socket);
	if (errorcode == DOWNLOAD_ERROR_IO_ERROR) {
		TRACE_ERROR("[CHECK IO] (%d)", download_id);
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		download_unset_state_changed_cb(download_id);
		return errorcode;
	}
	pthread_mutex_unlock((&g_download_ipc->mutex));
	if (errorcode == DOWNLOAD_ERROR_NONE) {
		// search same info in array.
		index = _get_my_slot_index(download_id);
		if (index < 0) {
			index = _get_empty_slot_index();
			if (index < 0) {
				TRACE_ERROR
				("[ERROR] TOO_MANY_DOWNLOADS[%d]", MAX_DOWNLOAD_HANDLE);
				pthread_mutex_unlock(&g_download_mutex);
				return DOWNLOAD_ERROR_TOO_MANY_DOWNLOADS;
			}
			g_download_slots[index].id = download_id;
		}
		g_download_slots[index].callback.state = callback;
		g_download_slots[index].callback.state_user_data = user_data;
	}
	pthread_mutex_unlock(&g_download_mutex);
	return errorcode;
}

int download_unset_state_changed_cb(int download_id)
{
	int index = -1;

	if (download_id <= 0) {
		TRACE_ERROR("[CHECK ID] (%d)", download_id);
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}

	pthread_mutex_lock(&g_download_mutex);
	TRACE_INFO("");

	if (_check_connections() != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	pthread_mutex_lock((&g_download_ipc->mutex));

#ifdef DOWNLOAD_ECHO_SUPPORT
	if (_check_ipc_status(g_download_ipc->cmd_socket) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
#endif

	index = _get_my_slot_index(download_id);
	if (index >= 0) {
		g_download_slots[index].callback.state = NULL;
		g_download_slots[index].callback.state_user_data = NULL;
	}
	// turn off state_cb flag of provider
	// send create command.
	if (_download_ipc_send_command(g_download_ipc->cmd_socket,
		download_id, DP_CMD_SET_STATE_CALLBACK)
		!= DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	if (_ipc_send_int(g_download_ipc->cmd_socket, 0) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	// return from provider.
	download_error_e errorcode =
		_download_ipc_read_return(g_download_ipc->cmd_socket);
	if (errorcode == DOWNLOAD_ERROR_IO_ERROR) {
		TRACE_ERROR("[CHECK IO] (%d)", download_id);
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return errorcode;
	}
	pthread_mutex_unlock((&g_download_ipc->mutex));
	pthread_mutex_unlock(&g_download_mutex);
	return errorcode;
}

int download_set_progress_cb(int download_id,
	download_progress_cb callback, void *user_data)
{
	int index = -1;

	if (download_id <= 0) {
		TRACE_ERROR("[CHECK ID] (%d)", download_id);
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}
	if (callback == NULL) {
		download_unset_progress_cb(download_id);
		return DOWNLOAD_ERROR_NONE;
	}

	pthread_mutex_lock(&g_download_mutex);
	TRACE_INFO("");

	if (_check_connections() != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	pthread_mutex_lock((&g_download_ipc->mutex));

#ifdef DOWNLOAD_ECHO_SUPPORT
	if (_check_ipc_status(g_download_ipc->cmd_socket) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
#endif

	// turn on progress_cb flag of provider
	// send create command.
	if (_download_ipc_send_command(g_download_ipc->cmd_socket,
		download_id, DP_CMD_SET_PROGRESS_CALLBACK)
		!= DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		download_unset_progress_cb(download_id);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	if (_ipc_send_int(g_download_ipc->cmd_socket, 1) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		download_unset_progress_cb(download_id);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	// return from provider.
	download_error_e errorcode =
		_download_ipc_read_return(g_download_ipc->cmd_socket);
	if (errorcode == DOWNLOAD_ERROR_IO_ERROR) {
		TRACE_ERROR("[CHECK IO] (%d)", download_id);
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		download_unset_progress_cb(download_id);
		return errorcode;
	}
	pthread_mutex_unlock((&g_download_ipc->mutex));
	if (errorcode == DOWNLOAD_ERROR_NONE) {
		// search same info in array.
		index = _get_my_slot_index(download_id);
		if (index < 0) {
			index = _get_empty_slot_index();
			if (index < 0) {
				TRACE_ERROR
				("[ERROR] TOO_MANY_DOWNLOADS[%d]", MAX_DOWNLOAD_HANDLE);
				pthread_mutex_unlock(&g_download_mutex);
				return DOWNLOAD_ERROR_TOO_MANY_DOWNLOADS;
			}
			g_download_slots[index].id = download_id;
		}

		g_download_slots[index].callback.progress = callback;
		g_download_slots[index].callback.progress_user_data = user_data;
	}
	pthread_mutex_unlock(&g_download_mutex);
	return errorcode;
}

int download_unset_progress_cb(int download_id)
{
	int index = -1;

	if (download_id <= 0) {
		TRACE_ERROR("[CHECK ID] (%d)", download_id);
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}

	pthread_mutex_lock(&g_download_mutex);
	TRACE_INFO("");

	if (_check_connections() != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	pthread_mutex_lock((&g_download_ipc->mutex));

#ifdef DOWNLOAD_ECHO_SUPPORT
	if (_check_ipc_status(g_download_ipc->cmd_socket) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
#endif

	index = _get_my_slot_index(download_id);
	if (index >= 0) {
		g_download_slots[index].callback.progress = NULL;
		g_download_slots[index].callback.progress_user_data = NULL;
	}
	// turn off progress_cb flag of provider
	// send create command.
	if (_download_ipc_send_command(g_download_ipc->cmd_socket,
		download_id, DP_CMD_SET_PROGRESS_CALLBACK)
		!= DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	if (_ipc_send_int(g_download_ipc->cmd_socket, 0) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	// return from provider.
	download_error_e errorcode =
		_download_ipc_read_return(g_download_ipc->cmd_socket);
	if (errorcode == DOWNLOAD_ERROR_IO_ERROR) {
		TRACE_ERROR("[CHECK IO] (%d)", download_id);
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return errorcode;
	}
	pthread_mutex_unlock((&g_download_ipc->mutex));
	pthread_mutex_unlock(&g_download_mutex);
	return errorcode;
}

int download_get_state(int download_id, download_state_e *state)
{
	int errorcode = DOWNLOAD_ERROR_NONE;
	dp_state_type dp_state = DP_STATE_NONE;

	if (download_id <= 0) {
		TRACE_ERROR("[CHECK ID] (%d)", download_id);
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}

	pthread_mutex_lock(&g_download_mutex);
	TRACE_INFO("");

	if (_check_connections() != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	pthread_mutex_lock((&g_download_ipc->mutex));

#ifdef DOWNLOAD_ECHO_SUPPORT
	if (_check_ipc_status(g_download_ipc->cmd_socket) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
#endif

	errorcode = _send_simple_cmd(download_id, DP_CMD_GET_STATE);
	if (errorcode != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errorcode == DOWNLOAD_ERROR_IO_ERROR && errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return errorcode;
	}
	// getting state with ID from provider.
	if (_ipc_read_custom_type(g_download_ipc->cmd_socket,
							&dp_state, sizeof(dp_state_type)) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	*state = _download_change_dp_state(dp_state);
	TRACE_INFO("ID : %d state : %d", download_id, *state);
	_download_print_str_state(*state);
	// it need to free
	pthread_mutex_unlock((&g_download_ipc->mutex));
	pthread_mutex_unlock(&g_download_mutex);
	return DOWNLOAD_ERROR_NONE;
}

int download_get_temp_path(int download_id, char **temp_path)
{
	int errorcode = DOWNLOAD_ERROR_NONE;
	char *value = NULL;

	if (download_id <= 0) {
		TRACE_ERROR("[CHECK ID] (%d)", download_id);
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}

	pthread_mutex_lock(&g_download_mutex);
	TRACE_INFO("");

	if (_check_connections() != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	pthread_mutex_lock((&g_download_ipc->mutex));

#ifdef DOWNLOAD_ECHO_SUPPORT
	if (_check_ipc_status(g_download_ipc->cmd_socket) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
#endif

	errorcode =
		_send_simple_cmd(download_id, DP_CMD_GET_TEMP_SAVED_PATH);
	if (errorcode != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errorcode == DOWNLOAD_ERROR_IO_ERROR && errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return errorcode;
	}
	// getting string with ID from provider.
	value = _ipc_read_string(g_download_ipc->cmd_socket);
	if (value == NULL) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	*temp_path = value;
	TRACE_INFO("ID : %d temp_path : %s", download_id, *temp_path);
	// it need to free
	pthread_mutex_unlock((&g_download_ipc->mutex));
	pthread_mutex_unlock(&g_download_mutex);
	return DOWNLOAD_ERROR_NONE;
}

int download_get_content_name(int download_id, char **content_name)
{
	int errorcode = DOWNLOAD_ERROR_NONE;
	char *value = NULL;

	if (download_id <= 0) {
		TRACE_ERROR("[CHECK ID] (%d)", download_id);
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}

	pthread_mutex_lock(&g_download_mutex);
	TRACE_INFO("");

	if (_check_connections() != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	pthread_mutex_lock((&g_download_ipc->mutex));

#ifdef DOWNLOAD_ECHO_SUPPORT
	if (_check_ipc_status(g_download_ipc->cmd_socket) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
#endif

	errorcode = _send_simple_cmd(download_id, DP_CMD_GET_CONTENT_NAME);
	if (errorcode != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errorcode == DOWNLOAD_ERROR_IO_ERROR && errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return errorcode;
	}
	// getting string with ID from provider.
	value = _ipc_read_string(g_download_ipc->cmd_socket);
	if (value == NULL) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	*content_name = value;
	TRACE_INFO("ID : %d content_name : %s", download_id, *content_name);
	// it need to free
	pthread_mutex_unlock((&g_download_ipc->mutex));
	pthread_mutex_unlock(&g_download_mutex);
	return DOWNLOAD_ERROR_NONE;
}

int download_get_content_size(int download_id,
	unsigned long long *content_size)
{
	int errorcode = DOWNLOAD_ERROR_NONE;

	if (download_id <= 0) {
		TRACE_ERROR("[CHECK ID] (%d)", download_id);
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}

	pthread_mutex_lock(&g_download_mutex);
	TRACE_INFO("");

	if (_check_connections() != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	pthread_mutex_lock((&g_download_ipc->mutex));

#ifdef DOWNLOAD_ECHO_SUPPORT
	if (_check_ipc_status(g_download_ipc->cmd_socket) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
#endif

	errorcode = _send_simple_cmd(download_id, DP_CMD_GET_TOTAL_FILE_SIZE);
	if (errorcode != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errorcode == DOWNLOAD_ERROR_IO_ERROR && errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return errorcode;
	}

	// getting content_size from provider.
	errorcode = _ipc_read_custom_type(g_download_ipc->cmd_socket,
		content_size, sizeof(unsigned long long));
	if (errorcode < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return errorcode;
	}
	TRACE_INFO("ID : %d content_size %lld", download_id, *content_size);
	pthread_mutex_unlock((&g_download_ipc->mutex));
	pthread_mutex_unlock(&g_download_mutex);
	return DOWNLOAD_ERROR_NONE;
}

int download_get_mime_type(int download_id, char **mime_type)
{
	int errorcode = DOWNLOAD_ERROR_NONE;
	char *value = NULL;

	if (download_id <= 0) {
		TRACE_ERROR("[CHECK ID] (%d)", download_id);
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}

	pthread_mutex_lock(&g_download_mutex);
	TRACE_INFO("");

	if (_check_connections() != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	pthread_mutex_lock((&g_download_ipc->mutex));

#ifdef DOWNLOAD_ECHO_SUPPORT
	if (_check_ipc_status(g_download_ipc->cmd_socket) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
#endif

	errorcode = _send_simple_cmd(download_id, DP_CMD_GET_MIME_TYPE);
	if (errorcode != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errorcode == DOWNLOAD_ERROR_IO_ERROR && errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return errorcode;
	}
	// getting state with ID from provider.
	value = _ipc_read_string(g_download_ipc->cmd_socket);
	if (value == NULL) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	*mime_type = value;
	TRACE_INFO("ID : %d mime_type : %s", download_id, *mime_type);
	// it need to free
	pthread_mutex_unlock((&g_download_ipc->mutex));
	pthread_mutex_unlock(&g_download_mutex);
	return DOWNLOAD_ERROR_NONE;
}

int download_set_auto_download(int download_id, bool enable)
{
	int value = enable;

	if (download_id <= 0) {
		TRACE_ERROR("[CHECK ID] (%d)", download_id);
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}

	pthread_mutex_lock(&g_download_mutex);
	TRACE_INFO("");

	if (_check_connections() != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	pthread_mutex_lock((&g_download_ipc->mutex));

#ifdef DOWNLOAD_ECHO_SUPPORT
	if (_check_ipc_status(g_download_ipc->cmd_socket) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
#endif

	if (_download_ipc_send_command(g_download_ipc->cmd_socket,
		download_id, DP_CMD_SET_AUTO_DOWNLOAD)
		!= DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	if (_ipc_send_int(g_download_ipc->cmd_socket, value) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	// return from provider.
	download_error_e errorcode =
		_download_ipc_read_return(g_download_ipc->cmd_socket);
	if (errorcode == DOWNLOAD_ERROR_IO_ERROR) {
		TRACE_ERROR("[CHECK IO] (%d)", download_id);
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return errorcode;
	}
	pthread_mutex_unlock((&g_download_ipc->mutex));
	pthread_mutex_unlock(&g_download_mutex);
	return errorcode;
}

int download_get_auto_download(int download_id, bool *enable)
{
	int errorcode = DOWNLOAD_ERROR_NONE;
	int value = 0;

	if (download_id <= 0) {
		TRACE_ERROR("[CHECK ID] (%d)", download_id);
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}

	pthread_mutex_lock(&g_download_mutex);
	TRACE_INFO("");

	if (_check_connections() != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	pthread_mutex_lock((&g_download_ipc->mutex));

#ifdef DOWNLOAD_ECHO_SUPPORT
	if (_check_ipc_status(g_download_ipc->cmd_socket) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
#endif

	errorcode = _send_simple_cmd(download_id, DP_CMD_GET_AUTO_DOWNLOAD);
	if (errorcode != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errorcode == DOWNLOAD_ERROR_IO_ERROR && errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return errorcode;
	}

	value = _download_ipc_read_int(g_download_ipc->cmd_socket);
	if (value < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	*enable = (bool)value;
	TRACE_INFO("ID : %d auto download : %d", download_id, *enable);
	pthread_mutex_unlock((&g_download_ipc->mutex));
	pthread_mutex_unlock(&g_download_mutex);
	return DOWNLOAD_ERROR_NONE;
}

int download_get_error(int download_id, download_error_e *error)
{
	download_error_e errorcode = DOWNLOAD_ERROR_NONE;

	if (download_id <= 0) {
		TRACE_ERROR("[CHECK ID] (%d)", download_id);
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}

	pthread_mutex_lock(&g_download_mutex);
	TRACE_INFO("");

	if (_check_connections() != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	pthread_mutex_lock((&g_download_ipc->mutex));

#ifdef DOWNLOAD_ECHO_SUPPORT
	if (_check_ipc_status(g_download_ipc->cmd_socket) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
#endif

	errorcode = _send_simple_cmd(download_id, DP_CMD_GET_ERROR);
	if (errorcode != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errorcode == DOWNLOAD_ERROR_IO_ERROR && errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return errorcode;
	}
	// getting errorcode from provider.
	errorcode =
		_download_ipc_read_return(g_download_ipc->cmd_socket);
	if (errorcode == DOWNLOAD_ERROR_IO_ERROR) {
		TRACE_ERROR("[CHECK IO] (%d)", download_id);
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return errorcode;
	}
	*error = errorcode;
	TRACE_INFO("ID : %d error : %d", download_id, *error);
	pthread_mutex_unlock((&g_download_ipc->mutex));
	pthread_mutex_unlock(&g_download_mutex);
	return DOWNLOAD_ERROR_NONE;
}

int download_get_http_status(int download_id, int *http_status)
{
	int errorcode = DOWNLOAD_ERROR_NONE;
	int status = 0;

	if (download_id <= 0) {
		TRACE_ERROR("[CHECK ID] (%d)", download_id);
		return DOWNLOAD_ERROR_INVALID_PARAMETER;
	}

	pthread_mutex_lock(&g_download_mutex);
	TRACE_INFO("");

	if (_check_connections() != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}

	pthread_mutex_lock((&g_download_ipc->mutex));

#ifdef DOWNLOAD_ECHO_SUPPORT
	if (_check_ipc_status(g_download_ipc->cmd_socket) < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
#endif

	errorcode = _send_simple_cmd(download_id, DP_CMD_GET_HTTP_STATUS);
	if (errorcode != DOWNLOAD_ERROR_NONE) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errorcode == DOWNLOAD_ERROR_IO_ERROR && errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return errorcode;
	}
	// getting int from provider.
	status = _download_ipc_read_int(g_download_ipc->cmd_socket);
	if (status < 0) {
		pthread_mutex_unlock((&g_download_ipc->mutex));
		if (errno != EAGAIN)
			_disconnect_from_provider();
		pthread_mutex_unlock(&g_download_mutex);
		return DOWNLOAD_ERROR_IO_ERROR;
	}
	*http_status = status;
	TRACE_INFO("ID : %d http_status : %d", download_id, *http_status);
	pthread_mutex_unlock((&g_download_ipc->mutex));
	pthread_mutex_unlock(&g_download_mutex);
	return DOWNLOAD_ERROR_NONE;
}

