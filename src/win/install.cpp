#define KC_INSTALLER_TITLE "chulengo installer"
#define KC_APP_ID "chulengo"

/**
 * install.cpp - Win64 installer implementation
 * Summary: Native installer logic embedded directly in each kc-bin package.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU GPL v3.0
 */

#define _WIN32_WINNT 0x0601

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <urlmon.h>

#ifndef KC_INSTALLER_TITLE
#error "KC_INSTALLER_TITLE must be defined before including install.cpp"
#endif

#ifndef KC_APP_ID
#error "KC_APP_ID must be defined before including install.cpp"
#endif

#define KC_ROUTING_URL "https://raw.githubusercontent.com/kaisarcode/kc-bin-dep/master/etc/installers.win64.json"
#define KC_MAX_FILES 16
#define KC_MAX_PATH_ENTRIES 8
#define KC_MAX_URL 2048
#define KC_MAX_TARGET 512

struct kc_manifest_file {
    char url[KC_MAX_URL];
    char target[KC_MAX_TARGET];
};

struct kc_manifest {
    char install_root[MAX_PATH];
    struct kc_manifest_file files[KC_MAX_FILES];
    int file_count;
    char path_entries[KC_MAX_PATH_ENTRIES][MAX_PATH];
    int path_count;
};

struct kc_ui {
    HWND window;
    HWND status_label;
    HWND progress_bar;
};

/**
 * Shows one installer message dialog.
 * @param kind Message box icon and behavior flags.
 * @param message Message text to display.
 * @return Does not return a value.
 */
static void kc_message_box(UINT kind, const char *message) {
    MessageBoxA(NULL, message, KC_INSTALLER_TITLE, kind | MB_OK);
}

/**
 * Shows one installer failure dialog.
 * @param message Failure text to display.
 * @return Does not return a value.
 */
static void kc_fail_box(const char *message) { kc_message_box(MB_ICONERROR, message); }

/**
 * Shows one installer success dialog.
 * @param message Success text to display.
 * @return Does not return a value.
 */
static void kc_success_box(const char *message) { kc_message_box(MB_ICONINFORMATION, message); }

/**
 * Checks whether the current process already has administrator rights.
 * @return 1 when the process is elevated, otherwise 0.
 */
static int kc_is_admin(void) {
    BOOL is_member = FALSE;
    SID_IDENTIFIER_AUTHORITY nt_authority = SECURITY_NT_AUTHORITY;
    PSID admin_group = NULL;

    if (!AllocateAndInitializeSid(&nt_authority, 2,
        SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0,
        &admin_group)) {
        return 0;
    }

    if (!CheckTokenMembership(NULL, admin_group, &is_member)) {
        is_member = FALSE;
    }

    FreeSid(admin_group);
    return is_member ? 1 : 0;
}

/**
 * Relaunches the installer requesting administrator privileges.
 * @return 0 when the elevated process was started, otherwise 1.
 */
static int kc_relaunch_as_admin(void) {
    char exe_path[MAX_PATH];
    SHELLEXECUTEINFOA sei;

    if (GetModuleFileNameA(NULL, exe_path, sizeof(exe_path)) == 0) {
        kc_fail_box("Unable to resolve installer path.");
        return 1;
    }

    memset(&sei, 0, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.lpVerb = "runas";
    sei.lpFile = exe_path;
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExA(&sei)) {
        kc_fail_box("Administrator privileges are required to install this app.");
        return 1;
    }

    return 0;
}

/**
 * Pumps the pending Windows message queue.
 * @return Does not return a value.
 */
static void kc_pump_messages(void) {
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

/**
 * Refreshes installer status text and progress.
 * @param ui Installer window handles.
 * @param status Current status message.
 * @param progress Progress position from 0 to 100.
 * @return Does not return a value.
 */
static void kc_ui_update(const struct kc_ui *ui, const char *status, int progress) {
    SetWindowTextA(ui->status_label, status);
    SendMessageA(ui->progress_bar, PBM_SETPOS, (WPARAM)progress, 0);
    UpdateWindow(ui->window);
    kc_pump_messages();
}

/**
 * Handles installer window messages.
 * @param hwnd Native window handle.
 * @param msg Windows message identifier.
 * @param wparam Message word parameter.
 * @param lparam Message long parameter.
 * @return Result code for the processed message.
 */
static LRESULT CALLBACK kc_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    (void)wparam;
    (void)lparam;
    if (msg == WM_CLOSE) {
        DestroyWindow(hwnd);
        return 0;
    }
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wparam, lparam);
}

/**
 * Creates the installer window and its controls.
 * @param instance Current process instance handle.
 * @param ui Output structure with created handles.
 * @return 0 on success, otherwise 1.
 */
static int kc_ui_init(HINSTANCE instance, struct kc_ui *ui) {
    WNDCLASSA wc;
    RECT rect = {0, 0, 420, 120};
    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);

    INITCOMMONCONTROLSEX icc;
    memset(&icc, 0, sizeof(icc));
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icc);

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = kc_window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "KcInstallerWindow";
    if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return 1;
    }

    AdjustWindowRect(&rect, WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    ui->window = CreateWindowExA(
        WS_EX_DLGMODALFRAME,
        wc.lpszClassName,
        KC_INSTALLER_TITLE,
        WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        (screen_w - (rect.right - rect.left)) / 2,
        (screen_h - (rect.bottom - rect.top)) / 2,
        rect.right - rect.left,
        rect.bottom - rect.top,
        NULL,
        NULL,
        instance,
        NULL
    );
    if (!ui->window) {
        return 1;
    }

    ui->status_label = CreateWindowExA(
        0, "STATIC", "Downloading installer manifest...",
        WS_CHILD | WS_VISIBLE,
        16, 16, 380, 20,
        ui->window, NULL, instance, NULL
    );
    ui->progress_bar = CreateWindowExA(
        0, PROGRESS_CLASSA, "",
        WS_CHILD | WS_VISIBLE,
        16, 50, 380, 22,
        ui->window, NULL, instance, NULL
    );
    if (!ui->status_label || !ui->progress_bar) {
        return 1;
    }

    SendMessageA(ui->progress_bar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    SendMessageA(ui->progress_bar, PBM_SETPOS, 0, 0);
    ShowWindow(ui->window, SW_SHOW);
    UpdateWindow(ui->window);
    kc_pump_messages();
    return 0;
}

/**
 * Downloads one file from a remote URL to disk.
 * @param url Remote URL to fetch.
 * @param path Destination file path.
 * @return 0 on success, otherwise 1.
 */
static int kc_download_file(const char *url, const char *path) {
    HRESULT hr = URLDownloadToFileA(NULL, url, path, 0, NULL);
    return hr == S_OK ? 0 : 1;
}

/**
 * Reads one whole file into memory.
 * @param path File path to read.
 * @return Heap buffer on success, otherwise NULL.
 */
static char *kc_read_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    long length;
    char *buffer;

    if (!fp) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    length = ftell(fp);
    if (length < 0) {
        fclose(fp);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    buffer = (char *)malloc((size_t)length + 1u);
    if (!buffer) {
        fclose(fp);
        return NULL;
    }
    if (length > 0 && fread(buffer, 1u, (size_t)length, fp) != (size_t)length) {
        free(buffer);
        fclose(fp);
        return NULL;
    }
    buffer[length] = '\0';
    fclose(fp);
    return buffer;
}

/**
 * Skips leading whitespace characters in one text buffer.
 * @param text Input text cursor.
 * @return Pointer to the first non-whitespace character.
 */
static const char *kc_skip_ws(const char *text) {
    while (text && *text && isspace((unsigned char)*text)) {
        text++;
    }
    return text;
}

/**
 * Parses one JSON string token into an output buffer.
 * @param text Input cursor expected to point at a JSON string.
 * @param out Output buffer for the decoded string.
 * @param out_size Capacity of the output buffer.
 * @param next_out Optional cursor advanced after the parsed token.
 * @return 0 on success, otherwise 1.
 */
static int kc_parse_json_string(const char *text, char *out, size_t out_size, const char **next_out) {
    size_t used = 0;

    if (!text || *text != '"' || !out || out_size == 0u) {
        return 1;
    }
    text++;
    while (*text && *text != '"') {
        char ch = *text++;
        if (ch == '\\') {
            if (*text == '\0') {
                return 1;
            }
            ch = *text++;
            if (ch == 'n') {
                ch = '\n';
            } else if (ch == 'r') {
                ch = '\r';
            } else if (ch == 't') {
                ch = '\t';
            } else if (ch != '\\' && ch != '"' && ch != '/') {
                return 1;
            } else {
            }
        }
        if (used + 1u >= out_size) {
            return 1;
        }
        out[used++] = ch;
    }
    if (*text != '"') {
        return 1;
    }
    out[used] = '\0';
    if (next_out) {
        *next_out = text + 1;
    }
    return 0;
}

/**
 * Finds the start of one quoted JSON key.
 * @param text JSON text to search.
 * @param key Key name without quotes.
 * @return Pointer to the matching key, otherwise NULL.
 */
static const char *kc_find_key(const char *text, const char *key) {
    size_t key_len = strlen(key);
    const char *cursor = text;

    while ((cursor = strstr(cursor, key)) != NULL) {
        if (cursor > text && cursor[-1] != '"') {
            cursor += key_len;
            continue;
        }
        if (cursor[key_len] != '"') {
            cursor += key_len;
            continue;
        }
        return cursor;
    }
    return NULL;
}

/**
 * Extracts one string property value from a JSON object.
 * @param json JSON object text.
 * @param key Property name without quotes.
 * @param out Output buffer for the decoded value.
 * @param out_size Capacity of the output buffer.
 * @return 0 on success, otherwise 1.
 */
static int kc_extract_string_value(const char *json, const char *key, char *out, size_t out_size) {
    const char *cursor = kc_find_key(json, key);

    if (!cursor) {
        return 1;
    }
    cursor += strlen(key) + 1;
    cursor = kc_skip_ws(cursor);
    if (*cursor != ':') {
        return 1;
    }
    cursor = kc_skip_ws(cursor + 1);
    return kc_parse_json_string(cursor, out, out_size, NULL);
}

/**
 * Parses one application manifest entry.
 * @param json JSON text for one application entry.
 * @param manifest Output manifest structure.
 * @return 0 on success, otherwise 1.
 */
static int kc_parse_manifest(const char *json, struct kc_manifest *manifest) {
    const char *files_key;
    const char *paths_key;
    const char *cursor;

    memset(manifest, 0, sizeof(*manifest));
    if (kc_extract_string_value(json, "install_root", manifest->install_root, sizeof(manifest->install_root)) != 0) {
        return 1;
    }

    files_key = kc_find_key(json, "files");
    if (!files_key) {
        return 1;
    }
    cursor = kc_skip_ws(files_key + strlen("files") + 1);
    if (*cursor != ':') {
        return 1;
    }
    cursor = kc_skip_ws(cursor + 1);
    if (*cursor != '[') {
        return 1;
    }
    cursor++;
    while (1) {
        cursor = kc_skip_ws(cursor);
        if (*cursor == ']') {
            break;
        }
        if (*cursor != '{' || manifest->file_count >= KC_MAX_FILES) {
            return 1;
        }
        if (kc_extract_string_value(cursor, "url",
                                    manifest->files[manifest->file_count].url,
                                    sizeof(manifest->files[manifest->file_count].url)) != 0 ||
            kc_extract_string_value(cursor, "target",
                                    manifest->files[manifest->file_count].target,
                                    sizeof(manifest->files[manifest->file_count].target)) != 0) {
            return 1;
        }
        manifest->file_count++;
        cursor = strchr(cursor, '}');
        if (!cursor) {
            return 1;
        }
        cursor++;
        cursor = kc_skip_ws(cursor);
        if (*cursor == ',') {
            cursor++;
            continue;
        }
        if (*cursor == ']') {
            break;
        }
        return 1;
    }

    paths_key = kc_find_key(json, "path_entries");
    if (paths_key) {
        cursor = kc_skip_ws(paths_key + strlen("path_entries") + 1);
        if (*cursor != ':') {
            return 1;
        }
        cursor = kc_skip_ws(cursor + 1);
        if (*cursor != '[') {
            return 1;
        }
        cursor++;
        while (1) {
            cursor = kc_skip_ws(cursor);
            if (*cursor == ']') {
                break;
            }
            if (manifest->path_count >= KC_MAX_PATH_ENTRIES ||
                kc_parse_json_string(cursor,
                    manifest->path_entries[manifest->path_count],
                    sizeof(manifest->path_entries[manifest->path_count]),
                    &cursor) != 0) {
                return 1;
            }
            manifest->path_count++;
            cursor = kc_skip_ws(cursor);
            if (*cursor == ',') {
                cursor++;
                continue;
            }
            if (*cursor == ']') {
                break;
            }
            return 1;
        }
    }

    return manifest->file_count == 0 ? 1 : 0;
}

/**
 * Parses the routing manifest and selects the current application entry.
 * @param json Full routing manifest text.
 * @param manifest Output manifest structure.
 * @return 0 on success, otherwise 1.
 */
static int kc_parse_routing_manifest(const char *json, struct kc_manifest *manifest) {
    const char *apps_key = kc_find_key(json, "apps");
    const char *cursor;
    char app_id[64];

    if (!apps_key) {
        return 1;
    }

    cursor = kc_skip_ws(apps_key + strlen("apps") + 1);
    if (*cursor != ':') {
        return 1;
    }
    cursor = kc_skip_ws(cursor + 1);
    if (*cursor != '[') {
        return 1;
    }
    cursor++;

    while (1) {
        cursor = kc_skip_ws(cursor);
        if (*cursor == ']') {
            break;
        }
        if (*cursor != '{') {
            return 1;
        }
        if (kc_extract_string_value(cursor, "app_id", app_id, sizeof(app_id)) != 0) {
            return 1;
        }
        if (strcmp(app_id, KC_APP_ID) == 0) {
            const char *entry_end = strchr(cursor, '}');
            size_t entry_len;
            char *entry_json;
            int rc;

            if (!entry_end) {
                return 1;
            }
            entry_len = (size_t)(entry_end - cursor + 1);
            entry_json = (char *)malloc(entry_len + 1u);
            if (!entry_json) {
                return 1;
            }
            memcpy(entry_json, cursor, entry_len);
            entry_json[entry_len] = '\0';
            rc = kc_parse_manifest(entry_json, manifest);
            free(entry_json);
            return rc;
        }

        cursor = strchr(cursor, '}');
        if (!cursor) {
            return 1;
        }
        cursor++;
        cursor = kc_skip_ws(cursor);
        if (*cursor == ',') {
            cursor++;
            continue;
        }
        if (*cursor == ']') {
            break;
        }
        return 1;
    }

    return 1;
}

/**
 * Creates all directories required by one path.
 * @param path Directory path to create.
 * @return 0 on success, otherwise 1.
 */
static int kc_ensure_directory(const char *path) {
    char temp[MAX_PATH];
    char *cursor;

    if (strlen(path) >= sizeof(temp)) {
        return 1;
    }
    strcpy(temp, path);
    for (cursor = temp + 3; *cursor != '\0'; cursor++) {
        if (*cursor == '\\' || *cursor == '/') {
            char saved = *cursor;
            *cursor = '\0';
            if (CreateDirectoryA(temp, NULL) == 0 && GetLastError() != ERROR_ALREADY_EXISTS) {
                return 1;
            }
            *cursor = saved;
        }
    }
    if (CreateDirectoryA(temp, NULL) == 0 && GetLastError() != ERROR_ALREADY_EXISTS) {
        return 1;
    }
    return 0;
}

/**
 * Appends one install directory entry to the system PATH.
 * @param install_root Installation root path.
 * @param entry Relative path entry to append.
 * @return 0 on success, otherwise 1.
 */
static int kc_append_path_entry(const char *install_root, const char *entry) {
    HKEY key;
    DWORD type = 0;
    DWORD size = 0;
    char path_value[8192];
    char full_entry[MAX_PATH];
    LONG status;

    if (PathCombineA(full_entry, install_root, entry) == NULL) {
        return 1;
    }
    status = RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
        0,
        KEY_QUERY_VALUE | KEY_SET_VALUE,
        &key);
    if (status != ERROR_SUCCESS) {
        return 1;
    }

    size = sizeof(path_value);
    status = RegQueryValueExA(key, "Path", NULL, &type, (LPBYTE)path_value, &size);
    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        path_value[0] = '\0';
        type = REG_EXPAND_SZ;
    } else {
        path_value[sizeof(path_value) - 1] = '\0';
    }

    if (StrStrIA(path_value, full_entry) == NULL) {
        size_t length = strlen(path_value);
        size_t needed = strlen(full_entry) + (length ? 1u : 0u) + 1u;
        if (length + needed >= sizeof(path_value)) {
            RegCloseKey(key);
            return 1;
        }
        if (length > 0) {
            path_value[length++] = ';';
            path_value[length] = '\0';
        }
        strcat(path_value, full_entry);
        status = RegSetValueExA(key, "Path", 0, REG_EXPAND_SZ,
                                (const BYTE *)path_value,
                                (DWORD)(strlen(path_value) + 1u));
        if (status != ERROR_SUCCESS) {
            RegCloseKey(key);
            return 1;
        }
        SendMessageTimeoutA(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                            (LPARAM)"Environment", SMTO_ABORTIFHUNG, 5000, NULL);
    }

    RegCloseKey(key);
    return 0;
}

/**
 * Downloads and installs every file listed in one manifest.
 * @param manifest Parsed manifest data.
 * @param ui Installer UI handles.
 * @return 0 on success, otherwise 1.
 */
static int kc_install_manifest_files(const struct kc_manifest *manifest, const struct kc_ui *ui) {
    int i;
    char target_dir[MAX_PATH];
    char target_path[MAX_PATH];
    char temp_path[MAX_PATH];
    char status[256];

    for (i = 0; i < manifest->file_count; i++) {
        if (PathCombineA(target_path, manifest->install_root, manifest->files[i].target) == NULL) {
            return 1;
        }
        strcpy(target_dir, target_path);
        PathRemoveFileSpecA(target_dir);
        if (kc_ensure_directory(target_dir) != 0) {
            return 1;
        }
        if (GetTempFileNameA(target_dir, "kci", 0, temp_path) == 0) {
            return 1;
        }

        snprintf(status, sizeof(status), "Downloading %s...", PathFindFileNameA(target_path));
        kc_ui_update(ui, status, 30 + ((i + 1) * 50) / manifest->file_count);

        if (kc_download_file(manifest->files[i].url, temp_path) != 0) {
            DeleteFileA(temp_path);
            return 1;
        }
        if (MoveFileExA(temp_path, target_path, MOVEFILE_REPLACE_EXISTING) == 0) {
            DeleteFileA(temp_path);
            return 1;
        }
    }
    return 0;
}

/**
 * Applies every PATH entry declared in one manifest.
 * @param manifest Parsed manifest data.
 * @return 0 on success, otherwise 1.
 */
static int kc_apply_path_entries(const struct kc_manifest *manifest) {
    int i;
    for (i = 0; i < manifest->path_count; i++) {
        if (kc_append_path_entry(manifest->install_root, manifest->path_entries[i]) != 0) {
            return 1;
        }
    }
    return 0;
}

/**
 * Runs the graphical installer entry point.
 * @param instance Current process instance handle.
 * @param prev Unused previous instance handle.
 * @param cmdline Raw command line string.
 * @param show Initial show state for the process window.
 * @return Process exit status.
 */
int WINAPI WinMain(HINSTANCE instance, HINSTANCE prev, LPSTR cmdline, int show) {
    struct kc_ui ui;
    struct kc_manifest manifest;
    char temp_dir[MAX_PATH];
    char routing_path[MAX_PATH];
    char *routing_json = NULL;
    int rc = 1;

    (void)prev;
    (void)cmdline;
    (void)show;

    if (!kc_is_admin()) {
        return kc_relaunch_as_admin();
    }

    if (kc_ui_init(instance, &ui) != 0) {
        kc_fail_box("Unable to initialize installer UI.");
        return 1;
    }

    if (GetTempPathA(sizeof(temp_dir), temp_dir) == 0 ||
        GetTempFileNameA(temp_dir, "kcr", 0, routing_path) == 0) {
        kc_fail_box("Unable to create temporary files.");
        return 1;
    }

    kc_ui_update(&ui, "Downloading installer manifest...", 10);
    if (kc_download_file(KC_ROUTING_URL, routing_path) != 0) {
        kc_fail_box("Unable to download the installer manifest.");
        goto cleanup;
    }

    kc_ui_update(&ui, "Reading installer manifest...", 20);
    routing_json = kc_read_file(routing_path);
    if (!routing_json || kc_parse_routing_manifest(routing_json, &manifest) != 0) {
        kc_fail_box("Unable to download or parse the installer manifests.");
        goto cleanup;
    }

    kc_ui_update(&ui, "Installing application files...", 30);
    if (kc_install_manifest_files(&manifest, &ui) != 0) {
        kc_fail_box("Unable to install application files.");
        goto cleanup;
    }

    kc_ui_update(&ui, "Updating environment...", 90);
    if (kc_apply_path_entries(&manifest) != 0) {
        kc_fail_box("Unable to update PATH.");
        goto cleanup;
    }

    kc_ui_update(&ui, "Installation complete.", 100);
    kc_success_box("Application installed successfully.");
    rc = 0;

cleanup:
    if (routing_json) {
        free(routing_json);
    }
    DeleteFileA(routing_path);
    DestroyWindow(ui.window);
    return rc;
}
