# Capture session ownership and I/O

`main` owns a `Capture` controller. It owns the terminal view, find/zoom UI state,
settings, and one `CaptureSession`. There are no file-level serial or terminal
owners. `CaptureSession` owns the logger, terminal data/state, raw RX capture,
connection state, and an I/O worker. The terminal objects exist before connecting,
so menus can safely use them in every connection state.
The existing core/view APIs share lifetime references to data, state, and logger;
the controller and session determine when those objects are replaced and closed.

`imterm_core` remains independent of the serial library and ImGui.
`imterm_session` adds session behavior and the `SerialTransport` abstraction. The
native adapter uses the existing serial parameter enums; broader settings
validation remains in Milestone 6. Tests substitute a synchronized byte loopback
or exercise the native adapter against Linux pseudo-terminals.

## Thread and queue boundaries

Only the worker constructs, opens, uses, and destroys the transport. It handles
reads, writes, modem queries, and DTR/RTS requests. It never touches ImGui,
terminal data/state, loggers, or raw capture files. Those belong to the main
thread. `Capture::Update`, called outside rendering, drains keyboard bytes and
calls `CaptureSession::Poll`. Poll consumes RX events, appends raw bytes before
parsing, updates terminal contents, and queues terminal replies.

The two directions use mutex-protected queues. Reads and individual write calls
are limited to 4 KiB. Pending RX and TX each have a 256 KiB budget, including an
in-flight TX chunk. The worker pauses reads when RX is full, leaving flow control
to the configured transport/device. It coalesces modem status/error events.
Oversized TX submissions are rejected as a whole with a visible error. Bytes
already accepted into the TX queue retain their order, including short writes.
A zero-byte write is treated as a timeout and follows the I/O failure policy.

Native reads/writes have 20 ms constant timeouts without byte-count multipliers.
The worker checks for cancellation between bounded calls and waits on a
condition variable when idle. Windows uses timed reads directly because the
bundled serial library does not implement `waitReadable` there. Unix readiness
checks also detect hangup when no bytes are available.

Open/configuration run on the worker too, so a slow OS driver does not block the
UI. Cancellation invalidates the attempt immediately; the worker releases a
stale connection as soon as its current OS call returns. Shutdown signals the
worker and joins it before session resources are destroyed. An OS driver that
never returns from open/close can still delay shutdown; no unsafe thread
termination is attempted.

## Connection transitions

| State | Meaning and allowed actions |
| --- | --- |
| Disconnected | No active connection; setup, connect, clear/find, logging, and raw capture are safe. |
| Connecting | An initial/manual open is pending. Cancel returns to Disconnected; duplicate connects and serial controls are disabled. |
| Connected | RX/TX, terminal responses, and supported modem controls are enabled. Disconnect, reconfigure, or change port ends the stream. |
| Reconnecting | An established connection failed. Retry after one steady-clock second, then after each failed retry. Cancel or disabling auto reconnect returns to Disconnected. |
| Reconfiguring | The previous transport is closing/closed and setup is available. Connect starts a new attempt; cancellation leaves it disconnected. |
| Failed | Initial/manual open failed, or connected I/O failed with auto reconnect disabled. The error remains visible and setup can retry. |

Manual disconnect never triggers auto reconnect. A manual open failure does not
retry automatically. Read/write exceptions share the same failure path. A failed
modem query or line-control request is reported separately and leaves the data
channel connected; PTYs and some adapters do not implement those ioctls.

CTS/DSR/DCD are cached by the worker every 250 ms while supported. DTR/RTS labels
reflect requested output values, not measurements. Requested line settings are
restored on reconnect and same-port reconfiguration. Hardware flow control owns
RTS, so manual RTS changes are disabled in that mode. A different port starts
with fresh line-control requests.

## Stream and data lifetime

Automatic reconnect, manual disconnect/reconnect to the same port, and same-port
parameter changes preserve terminal contents, cursor, and formatting. Finishing a
stream replaces incomplete UTF-8 prefixes, resets incomplete escape sequences,
discards unsent replies, and flushes incomplete log lines. No pending TX is
replayed after a failure, cancellation, or reconfiguration.

A successful change to a different port clears the buffer and resets protocol
state. It preserves newline mode, viewport size, view/log options, zoom, and
auto-scroll preferences. A failed port change keeps the old buffer available.
The log filename uses the port's basename rather than a full `/dev/...` path.
Raw capture remains active across connections until stopped or shut down.

Each request and event carries a generation. A change/cancellation discards old
queued work, including results from an in-flight open. Bytes already delivered
from an established stream are processed first. Cancelling an open discards a
queued success without resetting the previous buffer. Bytes still in an OS call or
waiting in the device are not promised to be captured. Normal shutdown flushes
terminal logging and raw capture and releases the worker's transport. Repeating
shutdown is safe.

The bundled serial library has a small integration fix on Unix and Windows:
if UART configuration throws after acquiring an OS handle, `open` releases that
handle before rethrowing. A Linux descriptor-count regression test covers this
partial-open path as well as normal session lifecycle tests under LeakSanitizer.
