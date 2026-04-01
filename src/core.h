/**
 * chulengo-core - Raw llama.cpp core interface
 * Summary: Exposes the compact command router used by the chulengo binary.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef CHULENGO_CORE_H
#define CHULENGO_CORE_H

/**
 * Runs the full chulengo command line.
 * @param argc Number of arguments.
 * @param argv Argument vector.
 * @return int Process exit status.
 */
int chulengo_main(int argc, char **argv);

#endif
