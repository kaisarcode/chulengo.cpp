# kc-app - Minimal Reference App

Minimal greeting application used to validate the base structure of a native
CLI app, including argument parsing, descriptor-based input, and descriptor-
based output.

## Resolution Order

`kc-app` always reads from the resolved input descriptor first.

Resolution order:

1. Read from `--fd-in <n>` when provided, otherwise read from `stdin`
2. If the resolved input produces a non-empty value, use that value
3. If the resolved input produces an empty value, fall back to `--name`
4. If `--name` is not provided, fall back to `World`

This is a blueprint application. It may block while waiting for stream input,
depending on the selected descriptor and the input sentinel used by the stream.

## Usage

### Default greeting fallback

```bash
printf '\n' | kc-app
Hello, World!
```

### Custom greeting fallback

```bash
printf '\n' | kc-app --name "John"
Hello, John!
```

### Descriptor input

```bash
exec 3<<<"Alice"
kc-app --fd-in 3
Hello, Alice!
```

### Standard input

```bash
echo "John Doe" | kc-app
Hello, John Doe!
```

### Descriptor output

```bash
exec 4>output.txt
printf '\n' | kc-app --name "John" --fd-out 4
cat output.txt
Hello, John!
```

## Full Parameter Reference

| Flag | Description | Default |
| :--- | :--- | :--- |
| `--name` | Fallback name used when input resolves empty | `World` |
| `--fd-in` | Input descriptor to read before applying fallbacks | `stdin` |
| `--fd-out` | Output descriptor for the greeting | `stdout` |
| `--help` | Show help and usage | `false` |

## Dependencies

- This program does not have external dependencies.

## Windows Installation

`kc-app` ships with a dedicated `install.exe`. Double click it on Windows and it will:

- open a native Windows installer window
- download a global installer routing manifest from `kc-bin-dep/etc`
- resolve the current `kc-app` manifest through that routing file
- download only the files declared by that manifest
- install them under `C:\Program Files\KaisarCode`
- register the required `PATH` entries

The installer does not embed the app payload. The manifests in the repo tell it what to download and where to place it.

---

**Author:** KaisarCode

**Email:** <kaisar@kaisarcode.com>

**Website:** [https://kaisarcode.com](https://kaisarcode.com)

**License:** [GNU GPL v3.0](https://www.gnu.org/licenses/gpl-3.0.html)

© 2026 KaisarCode
