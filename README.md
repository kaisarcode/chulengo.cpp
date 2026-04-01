# chulengo - Raw llama.cpp core binary

`chulengo` is a minimal raw binary built directly on top of `llama.cpp`.

It exposes only two operations:
- `chulengo embed`
- `chulengo infer`

It is not designed as a final product interface.
It does not expose descriptor routing, resident session control, internal prompt
templates, or wrapper protocols.

## Interface

The CLI follows a verb-first pattern:

```bash
chulengo embed [options]
chulengo infer [options]
```

Rules:
- `stdin` is always the dynamic input
- `stdout` is always the result channel
- `--type` defaults to `text`
- `image` is selected explicitly with `--type image`
- `EOT` (`ASCII 4`) is emitted after each completed result

## Embed

Text embedding from `stdin`:

```bash
printf 'delete log files' | chulengo embed --model ./models/bge-small.gguf
```

Image embedding from raw bytes on `stdin`:

```bash
cat ./samples/image.png | chulengo embed \
    --type image \
    --model ./models/jina-embeddings-v4-vllm-retrieval.Q4_K_M.gguf \
    --mmproj ./models/jina-embeddings-v4-vllm-retrieval.mmproj-Q8_0.gguf
```

Output is one raw JSON vector followed by `EOT`.

## Infer

Raw text inference from `stdin`:

```bash
printf 'Explain vector search in one sentence.' | chulengo infer \
    --model ./models/SmolLM2-135M-Instruct-Q4_K_M.gguf \
    --predict 48
```

Output is streamed directly to `stdout`, followed by a newline and `EOT`.

## Parameters

Shared flags:

| Flag | Description | Default |
| :--- | :--- | :--- |
| `--model` | Path to the GGUF model file | required |
| `--type` | Operation input type | `text` |
| `--mmproj` | Multimodal projector path when required | `NULL` |

Infer flags:

| Flag | Description | Default |
| :--- | :--- | :--- |
| `--ctx` | Context window | `2048` |
| `--predict` | Maximum generated tokens | `128` |
| `--threads` | CPU worker threads | `4` |
| `--gpu` | GPU layers offloaded | `999` |
| `--temp` | Temperature | `0.80` |
| `--top-k` | Top-K sampling | `40` |
| `--top-p` | Top-P sampling | `0.95` |
| `--penalty` | Repeat penalty | `1.10` |
| `--repeat-last-n` | Repeat window | `64` |
| `--seed` | RNG seed | `-1` |
| `--lora` | LoRA adapter path | `NULL` |
| `--lora-scale` | Scale for the previous LoRA entry | `1.0` |

## Dependencies

This binary links against:
- [`llama.cpp`](https://github.com/kaisarcode/kc-bin-dep/tree/slave/lib/llama.cpp)

## Local build

```bash
make x86_64
make aarch64
make arm64-v8a
make win64
make all
```

## Testing

```bash
./test.sh
```

---

**Author:** KaisarCode

**Email:** <kaisar@kaisarcode.com>

**Website:** [https://kaisarcode.com](https://kaisarcode.com)

**License:** [GNU GPL v3.0](https://www.gnu.org/licenses/gpl-3.0.html)

© 2026 KaisarCode
