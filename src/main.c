/**
 * chulengo - Raw llama.cpp entry point
 * Summary: Forwards the command line to the compact chulengo core.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#define _POSIX_C_SOURCE 200809L

#include "core.h"

/**
 * Runs the chulengo CLI entry point.
 * @param argc Number of arguments.
 * @param argv Argument vector.
 * @return int Process exit status.
 */
int main(int argc, char **argv) {
    return chulengo_main(argc, argv);
}
