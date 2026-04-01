![chulengo](./logo.png)

# chulengo - Raw llama.cpp core binary

`chulengo` is a small autonomous and portable base binary built directly on
top of `llama.cpp`.

It was created for a simple reason: in many local workflows, what is needed is
not a full product surface, but a dependable low-level binary that can be
connected to other tools directly.

`llama.cpp` already offers complete user-facing programs and that is valuable.
`chulengo` takes a different role. It focuses on exposing a smaller raw core
that is easy to compose from shells, pipes, and other local programs.

It exposes two core operations:
- `chulengo embed`
- `chulengo infer`

`chulengo` exists as a compact foundation for local composition: read from
`stdin`, write to `stdout`, finish the result, and remain easy to connect with
other programs.

The name comes from the guanaco, an Argentine relative of the llama. A baby
guanaco is called a `chulengo`.

## Interface

`chulengo` follows a verb-first shell pattern because it is meant to behave as
one raw engine primitive inside larger local compositions.

The contract is direct:
- one verb
- one stdin payload
- one stdout result
- one completed turn boundary with `EOT`

The CLI is:

```bash
chulengo embed [options]
chulengo infer [options]
```

Operational rules:
- `stdin` always carries the dynamic input
- `stdout` always carries the result
- `--type` configures the operation and defaults to `text`
- image input is selected explicitly with `--type image`
- each completed result ends with `EOT` (`ASCII 4`)

This keeps the binary predictable in pipes and leaves composition decisions to
the caller: how to route it, persist it, wrap it, or combine it with other
steps.

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

`chulengo` carries its native build dependencies inside this repository under
`lib/`.

Layout:
- `lib/inc` contains the headers used by the build
- `lib/obj` contains the shared runtime libraries used for the final link on
    each supported target

The final executables link against the vendored `llama.cpp`, `mtmd`, and
`ggml` runtime libraries stored there.

To refresh those runtime libraries from source:

```bash
./lib/build-deps.sh
```

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
