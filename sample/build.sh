#!/bin/sh

gcc -o url_download_test test.c -I./ `pkg-config --cflags --libs capi-web-url-download ecore gobject-2.0` -g


