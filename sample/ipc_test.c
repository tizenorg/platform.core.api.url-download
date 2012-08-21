#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>

#include "web/url_download.h"
#include "log.h"

bool exit_process_flag = false;

void download_manager_started_cb (url_download_h download, void *user_data)
{
    TRACE_DEBUG_MSG("started");
}
void download_manager_completed_cb (url_download_h download, const char * path, void *user_data)
{
    TRACE_DEBUG_MSG("download_manager_completed_cb (%s)",path);
    exit_process_flag = true;
}
void download_manager_progress_cb (url_download_h download, unsigned long long received, unsigned long long total, void *user_data)
{
    TRACE_DEBUG_MSG("progress (%d/%d)",received,total);
}

int main(int argc, char** argv)
{
    url_download_h download;
    exit_process_flag = false;
    // create download.
    url_download_create(&download);
    url_download_set_url(download, "abcdefghigk");
    url_download_set_destination(download, "1234567890everywhere");

    url_download_set_started_cb(download, download_manager_started_cb, NULL);
    url_download_set_completed_cb(download, download_manager_completed_cb, NULL);
    url_download_set_progress_cb(download, download_manager_progress_cb, NULL);

    // start....
    url_download_start(download);
    while(!exit_process_flag)
    {
        sleep(5);
    }
    // pasuse
    //url_download_pause(&download);
    // resume
    //url_download_stop(&download);

    url_download_destroy(download);
    TRACE_DEBUG_MSG("exit...........");
    exit(EXIT_SUCCESS);
}
