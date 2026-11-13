# Architecture and application lifetime

imterm supports Linux and Windows with the same terminal, session, and UI code.
Only platform startup, executable-path lookup, native serial I/O, and the bell
have platform-specific implementations. The UI thread owns the application and
ImGui; a session worker owns serial I/O.

## Modules and dependency direction

| CMake target | Owns or implements | First-party dependencies |
| --- | --- | --- |
| `imterm_core` | Terminal storage, protocol/parser, UTF-8, selection, keyboard policy, viewport calculations, palettes, logging/raw capture | None |
| `imterm_session` | Typed settings, capture session, bounded queues, worker, native serial adapter | `imterm_core` |
| `imterm_view` | ImGui keyboard adapter, renderer, coordinating terminal view | `imterm_core` |
| `imterm_application` | Capture UI, GLFW window, ImGui context/platform lifetime, Vulkan renderer, event loop, font/DPI setup | `imterm_session`, `imterm_view` |
| `imterm` | Platform entry point and top-level error reporting | `imterm_application` |

`imterm_backends` compiles the third-party ImGui GLFW/Vulkan adapters separately.
The core has no ImGui, Vulkan, or serial dependency; its only platform facility
is the bundled bell. Public session settings still expose serial enum types, so
`serial` is a public session dependency. There is no dependency from the core,
session, or view back to the application. Bundled libraries and imported packages
retain their own compiler policies.

See [session ownership](capture-session.md), [view components](terminal-view.md),
and [configuration validation](configuration.md) for the internal contracts.

## Startup and teardown

`RunApplication` creates these owners in order:

1. GLFW runtime, then a unique GLFW window.
2. ImGui context, then its GLFW platform backend.
3. `VulkanRenderer`, whose private implementation owns the instance, window
   surface, selected physical device reference, logical device/queue, descriptor
   pool, command pool/buffer, fence, acquire semaphore, render pass, swapchain,
   image views, framebuffers, per-image presentation semaphores, and ImGui Vulkan
   backend.
4. `Capture`, which owns its session and terminal UI state.

Every successfully created handle is recorded immediately. The renderer's
implementation exists before initialization starts; its destructor runs even
if initialization throws. Image resources are stored in zero-initialized owned
entries, so an error halfway through a swapchain image also unwinds safely.
ImGui backend user-data indicates whether partial backend teardown is needed.
Window and renderer owners cannot be copied.

Reverse destruction stops/joins the capture worker and flushes terminal/logging
state, waits for GPU work, shuts down the renderer backend, releases swapchain
resources and the device, destroys the surface and instance, shuts down the
platform backend, destroys the context/window, and terminates GLFW. The INI path
string outlives context destruction, which may save settings. Destructors do not
throw; device-loss waits are reported and cleanup continues. The entry point
reports exceptions to stderr and also a Windows message box before returning 1.

Startup checks GLFW/window availability, required Vulkan extensions, graphics
and presentation queue support, device creation, all Vulkan allocations,
surface formats/capabilities, backend initialization, and bundled-font loading.
A missing font falls back to ImGui's default. Validation layers are optional;
Debug builds do not require a developer Vulkan SDK installation.

## Rendering and resize

The renderer uses one in-flight submission, a reusable command buffer/fence and
acquire semaphore, and one completion semaphore per swapchain image. Completion
semaphores follow the acquired image index, as described in the
[Khronos synchronization guide](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html).
An out-of-date acquisition schedules a rebuild without submitting; a suboptimal
acquisition is consumed by submission/presentation before rebuilding. No fence
is reset until a submission is ready.

A nonzero framebuffer size change or an out-of-date/suboptimal presentation
rebuilds the swapchain after waiting for GPU work. ImGui and the compatible
render pass stay alive: backend shutdown also tears down GLFW viewport bindings
and therefore cannot be used for an ordinary resize. The ImGui render-buffer
ring is independent of swapchain image indices and safe with one submission in
flight. A minimized window waits for events without rendering. An unexpected
surface-format change reports an error and requires restarting the application.
The renderer uses Vulkan 1.0 and FIFO presentation; it retains the usual Vulkan
1.0 wait-idle cleanup convention rather than requiring newer presentation-fence
extensions. Multi-viewport rendering is disabled.

## Boundaries reviewed in milestone 8

Vulkan device/queue/image enumeration and image indices use unsigned counts or
container sizes, with checked API results and bounded enumeration retries.
Physical-device selection verifies presentation on the actual window surface.
External serial bytes, clipboard text, and configuration continue through their
existing validated boundaries. Reversed deletion ranges now reach their runtime
guard in both Debug and Release. The unused negative `Coordinates::Invalid`
sentinel, which contradicted that type's nonnegative constructor invariant, was
removed. Remaining coordinate/read-only assertions document internal invariants;
they do not replace parser, configuration, or native startup validation.
Read-only capture session access and zoom queries have const APIs.

## Build and validation policy

CMake 3.24 is the minimum, matching preset schema version 5. C++20, definitions,
include paths, warning policy, and sanitizer flags are target-scoped. Warnings
cover all first-party libraries, entry points, fuzzers and tests: `/W4` on MSVC,
`-Wall -Wextra -Wpedantic` on GCC/Clang. `IMTERM_WARNINGS_AS_ERRORS=ON` enables
`/WX` or `-Werror`, including when the ordinary warning option is disabled.
MSVC compiles first-party sources as UTF-8 and permits portable standard C
clock/environment functions through `_CRT_SECURE_NO_WARNINGS`.

[CI](../.github/workflows/cmake.yml) tests Linux GCC/Clang and Windows MSVC in
Debug and Release with both Conan and submodule GLFW/ImGui builds. Linux also
runs native PTY tests and real Vulkan lifecycle tests through Xvfb/lavapipe, with
Khronos validation errors failing the tests (including during teardown). A
separate Clang job runs ASan/UBSan and a bounded parser fuzz test. See the
[testing guide](../tests/README.md#continuous-integration) for reproduction and
remaining manual hardware/display checks.
