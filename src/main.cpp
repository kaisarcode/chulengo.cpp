/**
 * chulengo - Raw llama.cpp entry point
 * Summary: Implements the compact embed and infer operations for chulengo.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#define _POSIX_C_SOURCE 200809L

#include "pal.h"

#include <errno.h>
#include <limits.h>
#include <llama.h>
#ifdef CHULENGO_HAVE_MTMD
#include <mtmd-helper.h>
#include <mtmd.h>
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHULENGO_DEFAULT_CTX 2048
#define CHULENGO_DEFAULT_PREDICT 128
#define CHULENGO_DEFAULT_THREADS 4
#define CHULENGO_DEFAULT_GPU 999
#define CHULENGO_DEFAULT_REPEAT_LAST_N 64
#define CHULENGO_DEFAULT_TEMP 0.80f
#define CHULENGO_DEFAULT_TOP_K 40
#define CHULENGO_DEFAULT_TOP_P 0.95f
#define CHULENGO_DEFAULT_PENALTY 1.10f
#define CHULENGO_DEFAULT_SEED -1
#define CHULENGO_MAX_LORA 32

typedef enum {
    CHULENGO_COMMAND_NONE = 0,
    CHULENGO_COMMAND_EMBED,
    CHULENGO_COMMAND_INFER
} chulengo_command;

typedef enum {
    CHULENGO_TYPE_TEXT = 0,
    CHULENGO_TYPE_IMAGE
} chulengo_type;

typedef struct {
    chulengo_command command;
    chulengo_type type;
    const char *model_path;
    const char *mmproj_path;
    const char *kv_path;
    int n_ctx;
    int n_predict;
    int n_threads;
    int n_gpu_layers;
    float temperature;
    int top_k;
    float top_p;
    float repeat_penalty;
    int repeat_last_n;
    int seed;
    const char *lora_paths[CHULENGO_MAX_LORA];
    float lora_scales[CHULENGO_MAX_LORA];
    int lora_count;
} chulengo_config;

/**
 * Silences llama.cpp and mtmd logging.
 * @param level Backend log level.
 * @param text Backend log message.
 * @param user_data Opaque callback state.
 * @return void
 */
static void chulengo_log_silent(enum ggml_log_level level, const char *text, void *user_data) {
    (void)level;
    (void)text;
    (void)user_data;
}

/**
 * Prints the compact command help.
 * @return void
 */
static void chulengo_help(void) {
    printf("Usage:\n");
    printf("  chulengo embed [options]\n");
    printf("  chulengo infer [options]\n\n");
    printf("Commands:\n");
    printf("  embed                Emit one embedding vector in raw JSON\n");
    printf("  infer                Emit generated text directly to stdout\n\n");
    printf("Shared options:\n");
    printf("  --model <path>       Path to the GGUF model\n");
    printf("  --type <type>        Input type: text or image (default: text)\n");
    printf("  --mmproj <path>      Path to the multimodal projector when required\n");
    printf("  --help               Show help\n\n");
    printf("Infer options:\n");
    printf("  --kv <path>          Load one KV snapshot if present and save it back after inference\n");
    printf("  --ctx <int>          Context size (default: 2048)\n");
    printf("  --predict <int>      Maximum generated tokens (default: 128)\n");
    printf("  --threads <int>      CPU thread count (default: 4)\n");
    printf("  --gpu <int>          GPU layer count (default: 999)\n");
    printf("  --temp <float>       Temperature (default: 0.80)\n");
    printf("  --top-k <int>        Top-K sampling (default: 40)\n");
    printf("  --top-p <float>      Top-P sampling (default: 0.95)\n");
    printf("  --penalty <float>    Repeat penalty (default: 1.10)\n");
    printf("  --repeat-last-n <n>  Repeat window (default: 64)\n");
    printf("  --seed <int>         RNG seed (default: -1)\n");
    printf("  --lora <path>        LoRA adapter path\n");
    printf("  --lora-scale <f>     Scale for the previous LoRA adapter\n");
}

/**
 * Prints one CLI error.
 * @param message Error text.
 * @return int Always returns 1.
 */
static int chulengo_fail(const char *message) {
    fprintf(stderr, "Error: %s\n", message);
    return 1;
}

/**
 * Prints one CLI error followed by help.
 * @param message Error text.
 * @return int Always returns 1.
 */
static int chulengo_fail_usage(const char *message) {
    fprintf(stderr, "Error: %s\n\n", message);
    chulengo_help();
    return 1;
}

/**
 * Initializes the runtime configuration with defaults.
 * @param config Output configuration.
 * @return void
 */
static void chulengo_config_init(chulengo_config *config) {
    memset(config, 0, sizeof(*config));
    config->type = CHULENGO_TYPE_TEXT;
    config->n_ctx = CHULENGO_DEFAULT_CTX;
    config->n_predict = CHULENGO_DEFAULT_PREDICT;
    config->n_threads = CHULENGO_DEFAULT_THREADS;
    config->n_gpu_layers = CHULENGO_DEFAULT_GPU;
    config->temperature = CHULENGO_DEFAULT_TEMP;
    config->top_k = CHULENGO_DEFAULT_TOP_K;
    config->top_p = CHULENGO_DEFAULT_TOP_P;
    config->repeat_penalty = CHULENGO_DEFAULT_PENALTY;
    config->repeat_last_n = CHULENGO_DEFAULT_REPEAT_LAST_N;
    config->seed = CHULENGO_DEFAULT_SEED;
}

/**
 * Parses one strict integer.
 * @param text Raw input text.
 * @param out Parsed integer output.
 * @return int 0 on success, 1 on failure.
 */
static int chulengo_parse_int(const char *text, int *out) {
    char *end = NULL;
    long value = 0;

    if (!text || !text[0] || !out) {
        return 1;
    }
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' || value < INT_MIN || value > INT_MAX) {
        return 1;
    }
    *out = (int)value;
    return 0;
}

/**
 * Parses one strict float.
 * @param text Raw input text.
 * @param out Parsed float output.
 * @return int 0 on success, 1 on failure.
 */
static int chulengo_parse_float(const char *text, float *out) {
    char *end = NULL;
    float value = 0.0f;

    if (!text || !text[0] || !out) {
        return 1;
    }
    errno = 0;
    value = strtof(text, &end);
    if (errno != 0 || !end || *end != '\0') {
        return 1;
    }
    *out = value;
    return 0;
}

/**
 * Parses one operation type.
 * @param text Raw type text.
 * @param out Parsed type output.
 * @return int 0 on success, 1 on failure.
 */
static int chulengo_parse_type(const char *text, chulengo_type *out) {
    if (!text || !out) {
        return 1;
    }
    if (strcmp(text, "text") == 0) {
        *out = CHULENGO_TYPE_TEXT;
        return 0;
    }
    if (strcmp(text, "image") == 0) {
        *out = CHULENGO_TYPE_IMAGE;
        return 0;
    }
    return 1;
}

/**
 * Validates the parsed configuration.
 * @param config Parsed configuration.
 * @return int 0 on success, 1 on failure.
 */
static int chulengo_validate(const chulengo_config *config) {
    if (!config->model_path || !config->model_path[0]) {
        return chulengo_fail_usage("Missing required --model.");
    }
    if (config->command == CHULENGO_COMMAND_NONE) {
        return chulengo_fail_usage("Missing command. Use 'embed' or 'infer'.");
    }
    if (config->command == CHULENGO_COMMAND_EMBED && config->type == CHULENGO_TYPE_IMAGE) {
        if (!config->mmproj_path || !config->mmproj_path[0]) {
            return chulengo_fail_usage("Image embeddings require --mmproj.");
        }
    }
    if (config->command == CHULENGO_COMMAND_INFER && config->type != CHULENGO_TYPE_TEXT) {
        return chulengo_fail_usage("This first cut only supports 'infer --type text'.");
    }
    if (config->command != CHULENGO_COMMAND_INFER && config->kv_path) {
        return chulengo_fail_usage("KV state flags are only available for 'infer'.");
    }
    if (config->n_ctx < 0 || config->n_predict < 0) {
        return chulengo_fail_usage("Numeric values must be non-negative.");
    }
    if (config->n_threads <= 0 || config->n_gpu_layers < 0) {
        return chulengo_fail_usage("Invalid runtime thread or GPU value.");
    }
    if (config->temperature < 0.0f || config->top_k < 0) {
        return chulengo_fail_usage("Invalid sampling value.");
    }
    if (config->top_p <= 0.0f || config->top_p > 1.0f) {
        return chulengo_fail_usage("--top-p must be within (0, 1].");
    }
    if (config->repeat_penalty <= 0.0f || config->repeat_last_n < 0) {
        return chulengo_fail_usage("Invalid repeat penalty configuration.");
    }
    return 0;
}

/**
 * Parses the command line into the runtime configuration.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @param config Output configuration.
 * @return int 0 on success, 1 on failure.
 */
static int chulengo_parse_args(int argc, char **argv, chulengo_config *config) {
    int i = 0;

    if (argc < 2) {
        return chulengo_fail_usage("Missing command. Use 'embed' or 'infer'.");
    }
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        chulengo_help();
        return 2;
    }

    chulengo_config_init(config);

    if (strcmp(argv[1], "embed") == 0) {
        config->command = CHULENGO_COMMAND_EMBED;
    } else if (strcmp(argv[1], "infer") == 0) {
        config->command = CHULENGO_COMMAND_INFER;
    } else {
        return chulengo_fail_usage("Unknown command. Use 'embed' or 'infer'.");
    }

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            chulengo_help();
            return 2;
        }
        if (strcmp(argv[i], "--model") == 0) {
            if (i + 1 >= argc) {
                return chulengo_fail_usage("Missing value for --model.");
            }
            config->model_path = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--mmproj") == 0) {
            if (i + 1 >= argc) {
                return chulengo_fail_usage("Missing value for --mmproj.");
            }
            config->mmproj_path = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--kv") == 0) {
            if (i + 1 >= argc) {
                return chulengo_fail_usage("Missing value for --kv.");
            }
            config->kv_path = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--type") == 0) {
            if (i + 1 >= argc) {
                return chulengo_fail_usage("Missing value for --type.");
            }
            if (chulengo_parse_type(argv[++i], &config->type) != 0) {
                return chulengo_fail_usage("Unsupported value for --type.");
            }
            continue;
        }
        if (strcmp(argv[i], "--ctx") == 0) {
            if (i + 1 >= argc || chulengo_parse_int(argv[++i], &config->n_ctx) != 0) {
                return chulengo_fail_usage("Invalid value for --ctx.");
            }
            continue;
        }
        if (strcmp(argv[i], "--predict") == 0) {
            if (i + 1 >= argc || chulengo_parse_int(argv[++i], &config->n_predict) != 0) {
                return chulengo_fail_usage("Invalid value for --predict.");
            }
            continue;
        }
        if (strcmp(argv[i], "--threads") == 0) {
            if (i + 1 >= argc || chulengo_parse_int(argv[++i], &config->n_threads) != 0) {
                return chulengo_fail_usage("Invalid value for --threads.");
            }
            continue;
        }
        if (strcmp(argv[i], "--gpu") == 0) {
            if (i + 1 >= argc || chulengo_parse_int(argv[++i], &config->n_gpu_layers) != 0) {
                return chulengo_fail_usage("Invalid value for --gpu.");
            }
            continue;
        }
        if (strcmp(argv[i], "--temp") == 0) {
            if (i + 1 >= argc || chulengo_parse_float(argv[++i], &config->temperature) != 0) {
                return chulengo_fail_usage("Invalid value for --temp.");
            }
            continue;
        }
        if (strcmp(argv[i], "--top-k") == 0) {
            if (i + 1 >= argc || chulengo_parse_int(argv[++i], &config->top_k) != 0) {
                return chulengo_fail_usage("Invalid value for --top-k.");
            }
            continue;
        }
        if (strcmp(argv[i], "--top-p") == 0) {
            if (i + 1 >= argc || chulengo_parse_float(argv[++i], &config->top_p) != 0) {
                return chulengo_fail_usage("Invalid value for --top-p.");
            }
            continue;
        }
        if (strcmp(argv[i], "--penalty") == 0) {
            if (i + 1 >= argc || chulengo_parse_float(argv[++i], &config->repeat_penalty) != 0) {
                return chulengo_fail_usage("Invalid value for --penalty.");
            }
            continue;
        }
        if (strcmp(argv[i], "--repeat-last-n") == 0) {
            if (i + 1 >= argc || chulengo_parse_int(argv[++i], &config->repeat_last_n) != 0) {
                return chulengo_fail_usage("Invalid value for --repeat-last-n.");
            }
            continue;
        }
        if (strcmp(argv[i], "--seed") == 0) {
            if (i + 1 >= argc || chulengo_parse_int(argv[++i], &config->seed) != 0) {
                return chulengo_fail_usage("Invalid value for --seed.");
            }
            continue;
        }
        if (strcmp(argv[i], "--lora") == 0) {
            if (i + 1 >= argc) {
                return chulengo_fail_usage("Missing value for --lora.");
            }
            if (config->lora_count >= CHULENGO_MAX_LORA) {
                return chulengo_fail_usage("Too many --lora values.");
            }
            config->lora_paths[config->lora_count] = argv[++i];
            config->lora_scales[config->lora_count] = 1.0f;
            config->lora_count++;
            continue;
        }
        if (strcmp(argv[i], "--lora-scale") == 0) {
            if (config->lora_count == 0) {
                return chulengo_fail_usage("--lora-scale requires a previous --lora.");
            }
            if (i + 1 >= argc || chulengo_parse_float(argv[++i], &config->lora_scales[config->lora_count - 1]) != 0) {
                return chulengo_fail_usage("Invalid value for --lora-scale.");
            }
            continue;
        }
        return chulengo_fail_usage("Unknown argument.");
    }

    return chulengo_validate(config);
}

/**
 * Reads the full stdin stream into a heap buffer.
 * @param data Receives the allocated input payload.
 * @param size Receives the input size in bytes.
 * @return int 0 on success, 1 on failure.
 */
static int chulengo_read_stdin(unsigned char **data, size_t *size) {
    unsigned char block[4096];
    unsigned char *buffer = NULL;
    size_t used = 0;
    size_t capacity = 0;

    if (!data || !size) {
        return 1;
    }

    for (;;) {
        size_t count = fread(block, 1, sizeof(block), stdin);
        if (count > 0) {
            unsigned char *next = NULL;
            if (used + count + 1 > capacity) {
                capacity = capacity == 0 ? 4096u : capacity * 2u;
                while (capacity < used + count + 1) {
                    capacity *= 2u;
                }
                next = (unsigned char *)realloc(buffer, capacity);
                if (!next) {
                    free(buffer);
                    return 1;
                }
                buffer = next;
            }
            memcpy(buffer + used, block, count);
            used += count;
        }
        if (count < sizeof(block)) {
            if (ferror(stdin)) {
                free(buffer);
                return 1;
            }
            break;
        }
    }

    if (!buffer) {
        buffer = (unsigned char *)malloc(1u);
        if (!buffer) {
            return 1;
        }
    }

    buffer[used] = '\0';
    *data = buffer;
    *size = used;
    return 0;
}

/**
 * Removes trailing CR and LF bytes from one text payload.
 * @param data Mutable text payload.
 * @param size Mutable text size.
 * @return void
 */
static void chulengo_trim_text(unsigned char *data, size_t *size) {
    if (!data || !size) {
        return;
    }
    while (*size > 0 && (data[*size - 1] == '\n' || data[*size - 1] == '\r')) {
        (*size)--;
        data[*size] = '\0';
    }
}

/**
 * Writes one text fragment to stdout.
 * @param text Text bytes to write.
 * @param size Number of bytes to write.
 * @return int 0 on success, 1 on failure.
 */
static int chulengo_write_stdout(const void *text, size_t size) {
    if (size == 0) {
        return 0;
    }
    if (fwrite(text, 1, size, stdout) != size) {
        return 1;
    }
    return fflush(stdout) == 0 ? 0 : 1;
}

/**
 * Writes one EOT delimiter to stdout.
 * @return int 0 on success, 1 on failure.
 */
static int chulengo_write_eot(void) {
    unsigned char eot = CHULENGO_EOT;
    return chulengo_write_stdout(&eot, 1u);
}

/**
 * Tokenizes one text payload with automatic growth.
 * @param vocab Active vocabulary.
 * @param text Input text payload.
 * @param add_special Whether to add special tokens.
 * @param tokens_out Receives the allocated token buffer.
 * @param count_out Receives the token count.
 * @return int 0 on success, 1 on failure.
 */
static int chulengo_tokenize_dynamic(
    const struct llama_vocab *vocab,
    const char *text,
    bool add_special,
    llama_token **tokens_out,
    int *count_out
) {
    int capacity = 0;
    llama_token *tokens = NULL;
    int count = 0;

    if (!vocab || !text || !tokens_out || !count_out) {
        return 1;
    }

    capacity = (int)strlen(text) + 8;
    if (capacity < 16) {
        capacity = 16;
    }

    for (;;) {
        tokens = (llama_token *)malloc(sizeof(*tokens) * (size_t)capacity);
        if (!tokens) {
            return 1;
        }
        count = llama_tokenize(vocab, text, (int32_t)strlen(text), tokens, capacity, add_special, true);
        if (count >= 0) {
            *tokens_out = tokens;
            *count_out = count;
            return 0;
        }
        free(tokens);
        if (count == 0) {
            return 1;
        }
        capacity = (-count) + 8;
        if (capacity <= 0) {
            return 1;
        }
    }
}

/**
 * Resolves the best available embedding pointer from a context.
 * @param ctx Active llama context.
 * @param last_index Last token index when token embeddings are emitted.
 * @return float * Embedding pointer or NULL.
 */
static float *chulengo_get_embedding(struct llama_context *ctx, int last_index) {
    float *embedding = NULL;

    embedding = llama_get_embeddings_seq(ctx, 0);
    if (embedding) {
        return embedding;
    }
    embedding = llama_get_embeddings_ith(ctx, last_index);
    if (embedding) {
        return embedding;
    }
    return llama_get_embeddings(ctx);
}

/**
 * Prints one vector as compact JSON followed by EOT.
 * @param values Vector values.
 * @param count Number of values.
 * @return int 0 on success, 1 on failure.
 */
static int chulengo_emit_vector(const float *values, int count) {
    int i = 0;

    if (!values || count <= 0) {
        return 1;
    }
    if (fputc('[', stdout) == EOF) {
        return 1;
    }
    for (i = 0; i < count; i++) {
        if (fprintf(stdout, (i + 1 == count) ? "%f" : "%f,", values[i]) < 0) {
            return 1;
        }
    }
    if (fputs("]\n", stdout) == EOF) {
        return 1;
    }
    if (fflush(stdout) != 0) {
        return 1;
    }
    return chulengo_write_eot();
}

/**
 * Loads one llama model from disk.
 * @param config Active configuration.
 * @return struct llama_model * Loaded model or NULL.
 */
static struct llama_model *chulengo_load_model(const chulengo_config *config) {
    struct llama_model_params params = llama_model_default_params();

    params.n_gpu_layers = config->n_gpu_layers;
    return llama_model_load_from_file(config->model_path, params);
}

/**
 * Loads one KV state snapshot into the active context.
 * @param ctx Active llama context.
 * @param path KV state path.
 * @param n_past Receives the restored token count.
 * @return int 0 on success, 1 on failure.
 */
static int chulengo_load_kv_state(struct llama_context *ctx, const char *path, int *n_past) {
    FILE *file = NULL;
    llama_memory_t memory = {};
    llama_pos pos_max = -1;
    llama_token *tokens = NULL;
    size_t token_capacity = 0;
    size_t token_count = 0;

    if (!path || !path[0]) {
        if (n_past) {
            *n_past = 0;
        }
        return 0;
    }
    if (!ctx || !n_past) {
        return 1;
    }

    file = fopen(path, "rb");
    if (!file) {
        *n_past = 0;
        return 0;
    }
    fclose(file);

    token_capacity = (size_t)llama_n_ctx(ctx);
    if (token_capacity == 0) {
        token_capacity = 4096u;
    }
    tokens = (llama_token *)malloc(sizeof(*tokens) * token_capacity);
    if (!tokens) {
        return 1;
    }
    if (!llama_state_load_file(ctx, path, tokens, token_capacity, &token_count)) {
        free(tokens);
        return 1;
    }
    free(tokens);

    memory = llama_get_memory(ctx);
    pos_max = llama_memory_seq_pos_max(memory, 0);
    *n_past = pos_max >= 0 ? (int)pos_max + 1 : 0;
    llama_memory_seq_rm(memory, 0, *n_past, -1);
    return 0;
}

/**
 * Saves one KV state snapshot from the active context.
 * @param ctx Active llama context.
 * @param path KV state path.
 * @return int 0 on success, 1 on failure.
 */
static int chulengo_save_kv_state(struct llama_context *ctx, const char *path) {
    if (!path || !path[0]) {
        return 0;
    }
    if (!ctx) {
        return 1;
    }
    return llama_state_save_file(ctx, path, NULL, 0) ? 0 : 1;
}

/**
 * Applies every configured LoRA adapter to one context.
 * @param ctx Active llama context.
 * @param model Active llama model.
 * @param config Active configuration.
 * @param adapters Receives allocated adapter pointers.
 * @return int 0 on success, 1 on failure.
 */
static int chulengo_apply_lora(
    struct llama_context *ctx,
    struct llama_model *model,
    const chulengo_config *config,
    struct llama_adapter_lora **adapters
) {
    float scales[CHULENGO_MAX_LORA];
    int i = 0;

    if (config->lora_count == 0) {
        return 0;
    }

    for (i = 0; i < config->lora_count; i++) {
        adapters[i] = llama_adapter_lora_init(model, config->lora_paths[i]);
        if (!adapters[i]) {
            return 1;
        }
        scales[i] = config->lora_scales[i];
    }

    return llama_set_adapters_lora(ctx, adapters, (size_t)config->lora_count, scales) == 0 ? 0 : 1;
}

/**
 * Frees every loaded LoRA adapter.
 * @param adapters Adapter array.
 * @param count Number of valid entries.
 * @return void
 */
static void chulengo_free_lora(struct llama_adapter_lora **adapters, int count) {
    int i = 0;

    for (i = 0; i < count; i++) {
        if (adapters[i]) {
            llama_adapter_lora_free(adapters[i]);
        }
    }
}

/**
 * Runs one text embedding request.
 * @param config Active configuration.
 * @param input Input text payload.
 * @return int 0 on success, 1 on failure.
 */
static int chulengo_embed_text(const chulengo_config *config, const char *input) {
    struct llama_model *model = NULL;
    struct llama_context *ctx = NULL;
    struct llama_context_params params = llama_context_default_params();
    const struct llama_vocab *vocab = NULL;
    llama_token *tokens = NULL;
    int token_count = 0;
    int status = 1;
    int embedding_size = 0;
    float *embedding = NULL;

    model = chulengo_load_model(config);
    if (!model) {
        return chulengo_fail("Unable to load --model.");
    }

    params.n_ctx = 0;
    params.n_threads = config->n_threads;
    params.n_threads_batch = config->n_threads;
    params.embeddings = true;
    ctx = llama_init_from_model(model, params);
    if (!ctx) {
        llama_model_free(model);
        return chulengo_fail("Unable to initialize the embedding context.");
    }

    vocab = llama_model_get_vocab(model);
    if (chulengo_tokenize_dynamic(vocab, input, true, &tokens, &token_count) != 0 || token_count <= 0) {
        goto cleanup;
    }

    if (llama_model_has_encoder(model)) {
        struct llama_batch batch = llama_batch_init(token_count, 0, 1);
        int i = 0;
        batch.n_tokens = token_count;
        for (i = 0; i < token_count; i++) {
            batch.token[i] = tokens[i];
            batch.pos[i] = i;
            batch.n_seq_id[i] = 1;
            batch.seq_id[i][0] = 0;
            batch.logits[i] = (i + 1 == token_count);
        }
        if (llama_encode(ctx, batch) != 0) {
            llama_batch_free(batch);
            goto cleanup;
        }
        llama_batch_free(batch);
    } else {
        struct llama_batch batch = llama_batch_get_one(tokens, token_count);
        if (llama_decode(ctx, batch) != 0) {
            goto cleanup;
        }
    }

    embedding = chulengo_get_embedding(ctx, token_count - 1);
    if (!embedding) {
        goto cleanup;
    }

    embedding_size = llama_model_n_embd(model);
    if (embedding_size <= 0) {
        goto cleanup;
    }
    if (chulengo_emit_vector(embedding, embedding_size) != 0) {
        goto cleanup;
    }

    status = 0;

cleanup:
    free(tokens);
    llama_free(ctx);
    llama_model_free(model);
    if (status != 0) {
        return chulengo_fail("Unable to generate the text embedding.");
    }
    return 0;
}

#ifdef CHULENGO_HAVE_MTMD

/**
 * Runs one image embedding request from raw stdin bytes.
 * @param config Active configuration.
 * @param input Raw image bytes.
 * @param size Input size in bytes.
 * @return int 0 on success, 1 on failure.
 */
static int chulengo_embed_image(const chulengo_config *config, const unsigned char *input, size_t size) {
    struct llama_model *model = NULL;
    struct llama_context *ctx = NULL;
    struct llama_context_params llama_params = llama_context_default_params();
    struct mtmd_context_params mtmd_params = mtmd_context_params_default();
    mtmd_context *mtmd_ctx = NULL;
    mtmd_bitmap *bitmap = NULL;
    mtmd_input_chunks *chunks = NULL;
    mtmd_input_text text = {};
    const mtmd_bitmap *bitmaps[1];
    llama_pos new_n_past = 0;
    float *embedding = NULL;
    int embedding_size = 0;
    int status = 1;

    if (size == 0) {
        return chulengo_fail("Image embedding requires image bytes on stdin.");
    }

    model = chulengo_load_model(config);
    if (!model) {
        return chulengo_fail("Unable to load --model.");
    }

    llama_params.n_ctx = config->n_ctx > 0 ? (uint32_t)config->n_ctx : 0u;
    llama_params.n_threads = config->n_threads;
    llama_params.n_threads_batch = config->n_threads;
    llama_params.embeddings = true;
    ctx = llama_init_from_model(model, llama_params);
    if (!ctx) {
        llama_model_free(model);
        return chulengo_fail("Unable to initialize the multimodal context.");
    }

    mtmd_params.n_threads = config->n_threads;
    mtmd_params.use_gpu = config->n_gpu_layers != 0;
    mtmd_ctx = mtmd_init_from_file(config->mmproj_path, model, mtmd_params);
    if (!mtmd_ctx) {
        goto cleanup;
    }

    bitmap = mtmd_helper_bitmap_init_from_buf(mtmd_ctx, input, size);
    if (!bitmap) {
        goto cleanup;
    }

    chunks = mtmd_input_chunks_init();
    if (!chunks) {
        goto cleanup;
    }

    text.text = mtmd_default_marker();
    text.add_special = true;
    text.parse_special = true;
    bitmaps[0] = bitmap;

    if (mtmd_tokenize(mtmd_ctx, chunks, &text, bitmaps, 1) != 0) {
        goto cleanup;
    }
    if (mtmd_helper_eval_chunks(mtmd_ctx, ctx, chunks, 0, 0, config->n_ctx > 0 ? config->n_ctx : 2048, true, &new_n_past) != 0) {
        goto cleanup;
    }

    embedding = chulengo_get_embedding(ctx, (int)new_n_past - 1);
    embedding_size = llama_model_n_embd(model);
    if (!embedding || embedding_size <= 0) {
        goto cleanup;
    }
    if (chulengo_emit_vector(embedding, embedding_size) != 0) {
        goto cleanup;
    }

    status = 0;

cleanup:
    mtmd_input_chunks_free(chunks);
    mtmd_bitmap_free(bitmap);
    mtmd_free(mtmd_ctx);
    llama_free(ctx);
    llama_model_free(model);
    if (status != 0) {
        return chulengo_fail("Unable to generate the image embedding.");
    }
    return 0;
}
#endif

/**
 * Runs one embedding request.
 * @param config Active configuration.
 * @param input Raw stdin payload.
 * @param size Input size in bytes.
 * @return int 0 on success, 1 on failure.
 */
static int chulengo_run_embed(const chulengo_config *config, unsigned char *input, size_t size) {
    if (config->type == CHULENGO_TYPE_TEXT) {
        chulengo_trim_text(input, &size);
        if (size == 0) {
            return chulengo_fail("Text embedding requires non-empty stdin.");
        }
        return chulengo_embed_text(config, (const char *)input);
    }

#ifdef CHULENGO_HAVE_MTMD
    return chulengo_embed_image(config, input, size);
#else
    (void)input;
    (void)size;
    return chulengo_fail("This build does not include image embedding support.");
#endif
}

/**
 * Runs one inference request.
 * @param config Active configuration.
 * @param input Text input payload.
 * @return int 0 on success, 1 on failure.
 */
static int chulengo_run_infer(const chulengo_config *config, const char *input) {
    struct llama_model *model = NULL;
    struct llama_context *ctx = NULL;
    struct llama_context_params params = llama_context_default_params();
    struct llama_sampler *sampler = NULL;
    struct llama_adapter_lora *adapters[CHULENGO_MAX_LORA] = {0};
    const struct llama_vocab *vocab = NULL;
    llama_token *tokens = NULL;
    int token_count = 0;
    int n_past = 0;
    int status = 1;
    int i = 0;

    model = chulengo_load_model(config);
    if (!model) {
        return chulengo_fail("Unable to load --model.");
    }
    if (!llama_model_has_decoder(model)) {
        llama_model_free(model);
        return chulengo_fail("The selected model does not support inference.");
    }

    params.n_ctx = config->n_ctx > 0 ? (uint32_t)config->n_ctx : 0u;
    params.n_threads = config->n_threads;
    params.n_threads_batch = config->n_threads;
    ctx = llama_init_from_model(model, params);
    if (!ctx) {
        llama_model_free(model);
        return chulengo_fail("Unable to initialize the inference context.");
    }

    if (chulengo_apply_lora(ctx, model, config, adapters) != 0) {
        goto cleanup;
    }
    if (chulengo_load_kv_state(ctx, config->kv_path, &n_past) != 0) {
        goto cleanup;
    }

    vocab = llama_model_get_vocab(model);
    if (chulengo_tokenize_dynamic(vocab, input, n_past == 0, &tokens, &token_count) != 0 || token_count <= 0) {
        goto cleanup;
    }
    if (config->n_ctx > 0 && n_past + token_count >= config->n_ctx) {
        goto cleanup;
    }

    {
        struct llama_batch batch = llama_batch_init(token_count, 0, 1);
        batch.n_tokens = token_count;
        for (i = 0; i < token_count; i++) {
            batch.token[i] = tokens[i];
            batch.pos[i] = n_past + i;
            batch.n_seq_id[i] = 1;
            batch.seq_id[i][0] = 0;
            batch.logits[i] = (i + 1 == token_count);
        }
        if (llama_decode(ctx, batch) != 0) {
            llama_batch_free(batch);
            goto cleanup;
        }
        llama_batch_free(batch);
    }

    sampler = llama_sampler_chain_init(llama_sampler_chain_default_params());
    if (!sampler) {
        goto cleanup;
    }

    llama_sampler_chain_add(sampler, llama_sampler_init_penalties(config->repeat_last_n, config->repeat_penalty, 0.0f, 0.0f));
    llama_sampler_chain_add(sampler, llama_sampler_init_top_k(config->top_k));
    llama_sampler_chain_add(sampler, llama_sampler_init_top_p(config->top_p, 1));
    llama_sampler_chain_add(sampler, llama_sampler_init_temp(config->temperature));
    llama_sampler_chain_add(sampler, llama_sampler_init_dist((uint32_t)config->seed));

    for (i = 0; i < token_count; i++) {
        llama_sampler_accept(sampler, tokens[i]);
    }

    n_past += token_count;

    for (i = 0; i < config->n_predict; i++) {
        llama_token token = llama_sampler_sample(sampler, ctx, -1);
        char piece[256];
        int piece_size = 0;

        if (llama_vocab_is_eog(vocab, token)) {
            break;
        }

        llama_sampler_accept(sampler, token);
        piece_size = llama_token_to_piece(vocab, token, piece, sizeof(piece), 0, true);
        if (piece_size > 0 && chulengo_write_stdout(piece, (size_t)piece_size) != 0) {
            goto cleanup;
        }

        {
            struct llama_batch batch = llama_batch_init(1, 0, 1);
            batch.n_tokens = 1;
            batch.token[0] = token;
            batch.pos[0] = n_past;
            batch.n_seq_id[0] = 1;
            batch.seq_id[0][0] = 0;
            batch.logits[0] = 1;
            if (llama_decode(ctx, batch) != 0) {
                llama_batch_free(batch);
                goto cleanup;
            }
            llama_batch_free(batch);
        }
        n_past++;
    }

    if (chulengo_save_kv_state(ctx, config->kv_path) != 0) {
        goto cleanup;
    }

    if (fputc('\n', stdout) == EOF || fflush(stdout) != 0 || chulengo_write_eot() != 0) {
        goto cleanup;
    }

    status = 0;

cleanup:
    llama_sampler_free(sampler);
    free(tokens);
    chulengo_free_lora(adapters, config->lora_count);
    llama_free(ctx);
    llama_model_free(model);
    if (status != 0) {
        return chulengo_fail("Unable to complete inference.");
    }
    return 0;
}

/**
 * Runs the main chulengo control flow.
 * @param argc Number of arguments.
 * @param argv Argument vector.
 * @return int Process exit status.
 */
int main(int argc, char **argv) {
    chulengo_config config;
    unsigned char *input = NULL;
    size_t input_size = 0;
    int parse_status = 0;
    int status = 1;

    chulengo_prepare_stdio();
    llama_log_set(chulengo_log_silent, NULL);
#ifdef CHULENGO_HAVE_MTMD
    mtmd_helper_log_set(chulengo_log_silent, NULL);
#endif
    parse_status = chulengo_parse_args(argc, argv, &config);
    if (parse_status == 2) {
        return 0;
    }
    if (parse_status != 0) {
        return 1;
    }
    if (chulengo_read_stdin(&input, &input_size) != 0) {
        free(input);
        return chulengo_fail("Unable to read stdin.");
    }

    llama_backend_init();
    if (config.command == CHULENGO_COMMAND_EMBED) {
        status = chulengo_run_embed(&config, input, input_size);
    } else {
        if (input_size == 0) {
            free(input);
            llama_backend_free();
            return chulengo_fail("Inference requires text on stdin.");
        }
        status = chulengo_run_infer(&config, (const char *)input);
    }
    llama_backend_free();
    free(input);
    return status;
}
