# AGENTS.MD — C/C++ Desktop Application Rules

This document defines coding and architecture rules for C and C++ projects targeting PC desktop applications, including CLI tools, GUI applications, background utilities, and local services.

The rules are inspired by NASA’s “Power of 10” defensive programming principles, but adapted for desktop software where dynamic memory, operating-system APIs, event loops, files, threads, and user interfaces are expected.

These rules apply to all production code unless a local module document explicitly justifies an exception.

---

## 1. Core Principles

Write code that is easy to inspect, difficult to misuse, and predictable under failure.

Prefer simple control flow, explicit ownership, clear error handling, explicit types, and bounded resource usage. Desktop applications may be less constrained than embedded systems, but they still must remain robust under invalid input, low memory, missing files, permission failures, interrupted I/O, display scaling issues, and unexpected user actions.

Correctness, maintainability, and diagnosability are more important than cleverness.

---

## 2. Language and Standard

Use conservative, widely supported C and C++ standards.

For C projects, prefer C99 unless the repository explicitly selects a newer standard.

For C++ projects, prefer C++98 unless the repository explicitly selects a newer standard.

Do not require newer features unless the project explicitly documents that requirement.

Avoid language features that reduce readability, portability, or compiler compatibility.

The use of `auto` is forbidden in production code.

This rule applies even when the deduced type appears obvious. Explicit types are required because they make code review, static analysis, debugging, and long-term maintenance easier.

Forbidden:

```cpp
auto count = items.size();
auto it = values.begin();
auto result = load_config(path);
```

Required:

```cpp
std::size_t count = items.size();
std::vector<item_t>::iterator it = values.begin();
config_result_t result = load_config(path);
```

Do not use `auto` for variables, iterators, lambda parameters, return types, structured bindings, or generic lambdas.

Do not use C++17 structured bindings:

```cpp
auto [name, value] = entry; // forbidden
```

Use explicit access instead:

```cpp
std::string name = entry.first;
int value = entry.second;
```

Do not mix C and C++ styles casually. If a project uses both languages, keep the boundary explicit and documented.

C headers intended for use from C++ must support `extern "C"` guards.

---

## 3. Build Requirements

All code must compile cleanly with common warnings enabled.

At minimum, enable compiler warnings comparable to:

```text
-Wall
-Wextra
```

For MSVC, use a comparable warning level such as:

```text
/W4
```

Warnings must not be ignored. A warning is treated as a defect unless there is a documented reason.

Do not require pedantic mode unless the project explicitly opts into it.

Do not suppress warnings globally. Suppress narrowly, locally, and with a comment explaining why.

Debug and release builds must both work.

The selected C or C++ standard must be declared explicitly in the build system.

Examples:

```cmake
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
```

---

## 4. Project Structure

Use a clear directory structure. A typical layout may look like this:

```text
include/        Public headers
src/            Application source code
app/            CLI or GUI application entry points
lib/            Reusable internal libraries
tests/          Unit and integration tests
resources/      Icons, translations, UI files, static assets
docs/           Developer documentation
tools/          Helper scripts and developer utilities
```

Keep application entry points thin. Most logic should live in testable modules.

Do not hide core application behavior inside UI callbacks, command-line parsing code, or global initialization.

---

## 5. Naming Rules

Use clear, descriptive names.

Avoid Hungarian notation and artificial prefixes such as `g_`, `s_`, `m_`, or type-encoding prefixes.

The only allowed leading underscore convention is for private implementation details when the project explicitly uses that style. Do not use names reserved by the C or C++ standard, such as names beginning with double underscores or an underscore followed by an uppercase letter.

All custom type names should use the `_t` suffix.

Examples:

```c
typedef struct app_config app_config_t;
typedef enum app_mode app_mode_t;
```

```cpp
struct app_config_t;
class main_window_t;
enum class app_mode_t;
```

Use consistent casing within the project. Do not mix naming styles randomly.

---

## 6. Control Flow

Keep control flow simple and inspectable.

Avoid `goto` except for tightly controlled cleanup paths in C, where it improves clarity and reduces duplication.

Avoid recursion in production code unless all of the following are true:

1. The recursion depth is strictly bounded.
2. The bound is documented.
3. Iterative implementation would be less clear.
4. The recursion is covered by tests.

Prefer early returns for validation failures when they improve readability.

Avoid deeply nested blocks. If nesting exceeds three levels, consider extracting a function.

Do not use hidden control flow through macros, exceptions, global state, or callbacks unless the design explicitly requires it.

---

## 7. Function Size and Responsibility

Functions should be small enough to review easily.

A function should usually do one thing:

```text
Parse input
Validate data
Transform data
Render UI
Perform I/O
Handle one command
```

Avoid functions that parse, validate, modify state, perform I/O, update UI, and log errors all at once.

As a guideline, review any function longer than 60 lines. Refactor functions longer than 100 lines unless there is a strong reason not to.

Function names must describe the operation, not the implementation trick.

---

## 8. Data Ownership

Ownership must be obvious.

In C, document who owns allocated memory, handles, buffers, and objects.

In C++, prefer RAII. Use standard library ownership types where possible:

```cpp
std::unique_ptr<T>
std::shared_ptr<T>
std::vector<T>
std::string
std::lock_guard<T>
```

Avoid raw owning pointers in C++.

Raw pointers are acceptable for non-owning references when null is meaningful. Prefer references when null is not allowed.

Do not return pointers or references to temporary objects.

Do not store references to stack data beyond the lifetime of the stack frame.

Because `auto` is forbidden, ownership-related types must be written explicitly.

Forbidden:

```cpp
auto handle = open_file(path);
```

Required:

```cpp
file_handle_t handle = open_file(path);
```

---

## 9. Dynamic Memory

Dynamic memory is allowed in desktop software, but it must be controlled.

Do not allocate memory in tight loops without a clear reason.

Do not allocate memory repeatedly in rendering, polling, or high-frequency event paths when reuse is practical.

Check allocation failure in C.

In C++, prefer containers and RAII. If allocation failure must be handled explicitly, document the strategy.

Avoid manual `new` and `delete` in C++. Prefer named factory functions, smart pointers, containers, and value types.

Avoid unbounded growth. Caches, logs, queues, undo stacks, history buffers, and message lists must have limits or cleanup policies.

---

## 10. Error Handling

Every fallible operation must have an explicit error-handling path.

This includes:

```text
File access
Network access
Memory allocation
Configuration loading
Plugin loading
Process spawning
Thread creation
IPC
Database access
Graphics or window creation
User-provided input
Command-line arguments
Environment variables
```

Do not ignore return values from functions that can fail.

In C, use explicit status codes or result structs.

In C++, use one project-wide strategy:

```text
Exceptions
Error codes
Result-style structs
Optional-like wrappers
```

Do not mix error-handling styles randomly.

Exceptions must not cross C ABI boundaries.

User-facing errors must be understandable. Internal logs may contain technical detail, but UI messages should explain what failed and what the user can do.

---

## 11. Input Validation

Treat all external input as untrusted.

Validate:

```text
Command-line arguments
Config files
Project files
User documents
Drag-and-drop data
Clipboard data
Network responses
Environment variables
Registry values
IPC messages
Plugin metadata
GUI text fields
```

Validate before use.

Check sizes, ranges, encodings, paths, enum values, null values, and malformed structures.

Do not assume text is ASCII unless the interface explicitly requires ASCII. For desktop applications, UTF-8 should be preferred for internal text unless platform APIs require otherwise.

---

## 12. Bounds and Limits

All loops must have clear termination conditions.

Loops over external data must be bounded by validated sizes.

Do not process unbounded input without a size limit.

Examples of required limits:

```text
Maximum file size
Maximum line length
Maximum number of parsed records
Maximum number of recent files
Maximum number of open documents
Maximum undo history size
Maximum log file size
Maximum retry count
Maximum worker thread count
```

Limits should be named constants or configuration values, not magic numbers.

---

## 13. Filesystem Rules

Use safe and explicit filesystem handling.

Do not concatenate paths carelessly with raw string operations.

If the selected C++ standard and compiler support `std::filesystem`, it may be used. Otherwise, use a project-approved filesystem abstraction or platform-specific wrapper.

Do not introduce a newer C++ standard only to use `std::filesystem`.

Validate paths before use.

Handle missing files, permission errors, invalid filenames, read-only directories, symbolic links, locked files, and disk-full conditions.

Never overwrite user data without either:

1. An explicit user action, or
2. A documented autosave/backup policy.

Use atomic save patterns for important files:

```text
Write temporary file
Flush data
Close file
Replace target file
Keep backup if appropriate
```

---

## 14. CLI Application Rules

CLI applications must behave predictably in scripts.

Provide:

```text
--help
--version
Clear exit codes
Machine-readable output when requested
Human-readable output by default
```

Do not print progress bars, colors, prompts, or interactive UI when stdout is not a terminal unless explicitly requested.

Use stderr for diagnostics and stdout for primary output.

Do not require user interaction unless the command is explicitly interactive.

Do not change files unless the command clearly indicates a modifying operation.

Prefer dry-run support for destructive or bulk operations.

---

## 15. GUI Application Rules

GUI applications must keep the interface responsive.

Do not perform long-running work on the UI thread.

Move expensive operations to worker threads, async tasks, or job queues.

UI callbacks should be short. They should validate user intent, dispatch work, and update state.

Do not block the UI thread with:

```text
File scanning
Network access
Large parsing operations
Compression
Encryption
Database migration
External process execution
Long rendering tasks
```

All background work that affects UI state must return results through a safe synchronization mechanism.

The GUI must handle cancellation where practical.

User-visible operations should provide progress feedback when they may take noticeable time.

---

## 16. Threading and Concurrency

Concurrency must be designed, not accidental.

Document which thread owns each major object or subsystem.

Protect shared mutable data with clear synchronization.

Prefer message passing, immutable data, task queues, or actor-like ownership over shared mutable state.

Do not access GUI objects from worker threads unless the GUI framework explicitly allows it.

Avoid detached threads unless the application has a controlled shutdown mechanism.

Every started thread, task, or worker must have a defined lifetime and shutdown path.

Avoid data races. Treat data races as critical defects.

Because `auto` is forbidden, lock and thread helper types must be explicit.

Forbidden:

```cpp
auto lock = std::lock_guard<std::mutex>(mutex);
```

Required:

```cpp
std::lock_guard<std::mutex> lock(mutex);
```

---

## 17. Global State

Avoid mutable global state.

Allowed global state:

```text
Compile-time constants
Read-only lookup tables
Process-wide configuration after initialization
Logger instance, if thread-safe and controlled
```

Avoid hidden dependencies on global variables.

Do not use global state to pass data between unrelated modules.

For tests, global state must be resettable or isolated.

---

## 18. Macros

Use macros sparingly.

Allowed uses include:

```text
Include guards
Platform detection
Feature flags
Simple compile-time constants in C
Logging wrappers when necessary
```

Do not use macros to hide control flow, declare variables unexpectedly, or create type-unsafe APIs.

In C++, prefer typed constants, enums, inline functions, and ordinary functions where possible.

Use `constexpr` only when supported by the selected C++ standard and when it does not reduce readability.

Macro names must be uppercase and project-prefixed where appropriate.

---

## 19. Type Safety

Use strong types where practical.

Avoid passing unrelated values as raw integers or strings when an enum, struct, class, or dedicated type would be clearer.

Use fixed-width integer types when binary layout, file format, protocol format, or exact size matters:

```c
uint8_t
uint16_t
uint32_t
int64_t
```

Do not assume `int`, `long`, pointer size, endianness, or alignment across platforms.

Use explicit casts. Avoid C-style casts in C++.

Do not use `auto` to hide the result type of expressions, factory functions, iterators, casts, lambdas, or API calls.

Forbidden:

```cpp
auto value = static_cast<uint32_t>(raw_value);
```

Required:

```cpp
uint32_t value = static_cast<uint32_t>(raw_value);
```

---

## 20. Integer and Floating-Point Safety

Check integer conversions and arithmetic where overflow matters.

Be careful with:

```text
File sizes
Buffer sizes
Image dimensions
Array indexes
Pointer offsets
Time calculations
Memory allocation sizes
```

Do not convert signed to unsigned values without validation.

Do not compare floating-point values for exact equality unless the values are discrete by construction.

Use named tolerances for approximate comparisons.

---

## 21. Strings and Encoding

Prefer safe string types.

In C++, use:

```cpp
std::string
std::wstring where required by platform APIs
```

Use `std::string_view`, `std::u8string`, or newer string features only if the selected C++ standard supports them and the project explicitly allows them.

In C, always pass buffer length together with writable buffers.

Do not use unsafe functions such as:

```c
gets
strcpy
strcat
sprintf
```

Prefer bounded alternatives and explicit formatting.

Handle encoding boundaries deliberately. Document whether files, CLI arguments, UI text, and internal strings are UTF-8, UTF-16, or platform-native.

---

## 22. Resource Management

Every acquired resource must have a matching release path.

Resources include:

```text
Memory
Files
Sockets
Windows
GUI handles
Threads
Mutexes
Processes
Pipes
Temporary files
Database handles
Graphics resources
Plugin handles
```

In C++, use RAII wrappers.

In C, use cleanup functions and consistent ownership conventions.

Cleanup must work on partial initialization failure.

Application shutdown must be orderly.

---

## 23. Logging and Diagnostics

Applications must provide useful diagnostics.

Logs should include enough context to diagnose problems:

```text
Operation being performed
Relevant file path or command
Error code or exception message
Subsystem name
```

Do not log secrets, passwords, tokens, private user content, or excessive personal data.

Do not spam logs inside high-frequency loops.

GUI applications should not expose raw internal errors to users unless useful. Provide friendly messages and keep technical details in logs.

CLI applications should produce clear stderr diagnostics.

---

## 24. Security and Privacy

Assume local input can be malicious.

Do not execute files, scripts, commands, plugins, or shell operations from untrusted paths without validation and explicit intent.

Avoid shell invocation when direct process APIs are available.

Sanitize command arguments passed to external processes.

Do not store secrets in plain text unless the project explicitly accepts that risk. Prefer OS credential storage.

Do not transmit telemetry, diagnostics, or crash reports without clear user consent if the application supports such features.

---

## 25. Platform Abstraction

Keep platform-specific code isolated.

Use small abstraction layers for:

```text
Filesystem quirks
Process launching
Native dialogs
Registry or system settings
Window integration
Clipboard
Notifications
Credential storage
Dynamic libraries
```

Avoid scattering `#ifdef _WIN32`, `#ifdef __linux__`, or `#ifdef __APPLE__` throughout business logic.

Each platform-specific path must be testable or manually verifiable.

---

## 26. GUI Framework Integration

Framework-specific code must not dominate business logic.

Keep UI layer responsibilities limited to:

```text
Displaying state
Receiving user input
Dispatching commands
Showing progress
Showing errors
```

Keep application logic in framework-independent modules where practical.

Do not store core state only inside widgets.

Do not make business logic depend on visual layout.

For Model-View-style frameworks, keep model state explicit and testable.

---

## 27. Configuration

Configuration loading must be robust.

Handle:

```text
Missing config
Old config version
Unknown fields
Invalid values
Partial corruption
Permission errors
```

Use versioned config formats where practical.

Never fail silently when configuration cannot be loaded. Either use documented defaults or report the problem.

Do not require users to manually edit configuration files unless this is intentional for the application.

---

## 28. Serialization and File Formats

Project files, save files, cache files, and exported files must be versioned.

Parsers must reject malformed data safely.

Do not trust sizes, offsets, counts, enum values, or embedded paths from files.

Forward and backward compatibility rules must be documented when applicable.

Avoid binary formats unless there is a strong reason. If using binary formats, document endianness, alignment, version, and field sizes.

---

## 29. Tests

Write tests for logic that can be tested without the full UI.

Prioritize tests for:

```text
Parsers
Serializers
Path handling
Command-line parsing
Configuration loading
State machines
Error handling
Data transformations
Boundary conditions
Regression bugs
```

GUI behavior may be tested through integration tests, snapshot tests, or manual test procedures if automated testing is impractical.

Every bug fix should include a regression test when practical.

---

## 30. Static Analysis and Formatting

Use automatic formatting.

Do not manually align code in a way that fights the formatter.

Use static analysis where practical:

```text
clang-tidy
cppcheck
MSVC Code Analysis
sanitizers
```

Address analyzer warnings or document why they are false positives.

Recommended sanitizers for debug or CI builds:

```text
AddressSanitizer
UndefinedBehaviorSanitizer
ThreadSanitizer where practical
```

Static analysis should flag forbidden `auto` usage where possible.

---

## 31. Comments and Documentation

Comments should explain why, not restate what the code already says.

Use comments for:

```text
Non-obvious decisions
Platform quirks
Algorithm constraints
Threading assumptions
Ownership rules
Security-sensitive behavior
Compatibility requirements
```

Public APIs must have enough documentation for correct use.

Do not leave misleading comments. Update comments when code changes.

Remove dead code instead of commenting it out.

---

## 32. Public APIs

Public APIs must be hard to misuse.

Validate arguments at module boundaries.

Document ownership, lifetime, threading, error behavior, and encoding.

Keep APIs small and stable.

Do not expose internal data structures unless necessary.

Prefer explicit configuration structs over long parameter lists.

Avoid APIs whose return type is unclear at call sites. Since `auto` is forbidden, function return types should remain readable and not excessively complex.

---

## 33. Dependencies

Keep dependencies intentional.

Before adding a dependency, consider:

```text
License
Maintenance status
Platform support
Binary size
Security history
Build complexity
Transitive dependencies
Required language standard
```

Do not add dependencies that force a newer C or C++ standard unless the project explicitly accepts that requirement.

Do not add large frameworks for small utilities unless justified.

Wrap third-party APIs when direct use would spread dependency-specific code throughout the project.

Pin dependency versions where practical.

---

## 34. Performance

Optimize only with evidence.

Prefer clear code first, then measure.

Avoid obvious inefficiencies in hot paths:

```text
Repeated allocations
Unnecessary copies
Blocking UI thread
Repeated filesystem scans
Excessive logging
Unbounded cache growth
```

Use profiling tools before major optimization work.

Do not trade correctness or maintainability for small unmeasured speed gains.

---

## 35. Accessibility and UX Robustness

GUI applications should remain usable under normal desktop variations.

Consider:

```text
High-DPI displays
Keyboard navigation
Screen readers where practical
Color contrast
Window resizing
Long translations
Different system fonts
Dark and light themes if supported
```

Do not encode critical meaning only through color.

CLI tools should provide clear help text and stable behavior suitable for scripts.

---

## 36. Internationalization

Do not hardcode user-facing text deep inside business logic.

Keep user-facing strings separate where practical.

Support UTF-8 text internally unless the platform or framework requires otherwise.

Avoid assumptions about date, time, decimal separator, path separator, or text direction.

Use locale-aware formatting for user-visible values.

Use stable, locale-independent formatting for machine-readable files and logs where needed.

---

## 37. Time Handling

Use monotonic clocks for durations and timeouts.

Use wall-clock time only for calendar time, timestamps, schedules, and user-visible dates.

Do not use local time for persistent machine-readable timestamps unless required.

Store timestamps in a clear format, preferably UTC with explicit offset or ISO 8601.

Handle daylight saving time and timezone differences when scheduling or displaying times.

---

## 38. Event Loops and Callbacks

Callbacks must be short and predictable.

Do not hide complex state transitions inside anonymous callbacks.

If a callback does more than dispatch or adapt data, consider extracting a named function.

Avoid callback chains that obscure error handling.

For event-driven code, document the state machine or command flow.

Avoid generic lambdas because they require `auto` parameters.

Forbidden:

```cpp
[](auto value) {
    process(value);
}
```

Required:

```cpp
[](const event_t& value) {
    process(value);
}
```

---

## 39. Shutdown Behavior

Applications must shut down cleanly.

On exit:

```text
Stop background workers
Cancel pending operations
Flush important logs
Save user data if appropriate
Release OS resources
Close files and sockets
Destroy GUI resources in the correct order
```

Do not abruptly terminate worker threads unless there is no safe alternative.

Handle shutdown during active operations.

---

## 40. Review Checklist

Before code is accepted, verify:

```text
The code compiles without warnings.
The selected C/C++ standard is respected.
No forbidden `auto` usage was introduced.
Error paths are handled.
External input is validated.
Loops are bounded or obviously finite.
Ownership is clear.
Resources are released.
UI thread is not blocked by long work.
Shared state is synchronized.
User-facing errors are understandable.
No secrets or private data are logged.
Tests or manual verification cover important behavior.
```

---

## 41. Exceptions to These Rules

Exceptions are allowed only when they are deliberate and documented.

An exception must explain:

```text
Which rule is being bypassed
Why the exception is necessary
What risk is introduced
How the risk is controlled
```

Do not create exceptions silently.

The `auto` ban should not be bypassed for convenience. Exceptions should be limited to unavoidable third-party macro interactions or compiler/library constraints.

---

## 42. Agent Instructions

When modifying this repository, an AI or automation agent must follow these rules:

1. Prefer small, reviewable changes.
2. Do not rewrite unrelated code.
3. Preserve existing style unless it conflicts with this document.
4. Do not introduce new dependencies without justification.
5. Do not require a newer C/C++ standard without explicit approval.
6. Do not use `auto`.
7. Do not ignore compiler warnings.
8. Do not remove validation, logging, tests, or error handling to simplify a change.
9. Do not make GUI code block the UI thread.
10. Do not add global mutable state unless explicitly justified.
11. Do not change public APIs without documenting the compatibility impact.
12. Update tests and documentation when behavior changes.

When uncertain, choose the safer, simpler, more explicit implementation.
