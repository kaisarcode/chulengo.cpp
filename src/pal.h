/**
 * kc-app-pal - Platform Abstraction Layer
 * Summary: Descriptor and I/O portability helpers for kc-app.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU GPL v3.0
 */

#ifndef KC_APP_PAL_H
#define KC_APP_PAL_H

#ifdef _WIN32
#include <io.h>
#define KC_APP_READ _read
#define KC_APP_WRITE _write
#define KC_APP_STDIN_FD 0
#define KC_APP_STDOUT_FD 1
#else
#include <unistd.h>
#define KC_APP_READ read
#define KC_APP_WRITE write
#define KC_APP_STDIN_FD STDIN_FILENO
#define KC_APP_STDOUT_FD STDOUT_FILENO
#endif

#endif
