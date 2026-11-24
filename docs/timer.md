# `timer`

How the C layer arms the core's next deadline. The core owns no clock and takes time only as a number its caller hands
in, so something outside it must run when a deadline falls due; the rules that timer keeps in any language are the
core's `docs/contract/timer.md`.

`weivel_c_timer_create` arms the core's deadline on the caller's `weivel_c_scheduler`: a clock, `now`, and
`schedule` and `cancel` for one task. The layer owns no thread and no timer. The task is scheduled from the latest
deadline drained and from nothing else, replaced when the deadline moves and cancelled when none is left; when it
runs it hands the core the clock's time, and when the scheduler says it cannot wait the whole way (`maximum_wait`)
it waits as long as it may and arms again. While a deadline is armed the wrapper reads the clock before each call
and advances the core first when the deadline has passed, so a registration at or after a want's deadline settles
nothing even when the task has not run. `weivel_c_timer_destroy` cancels the task, so nothing of the layer's fires
once its core is gone. A `weivel_c_timer_observer` given at creation hears each move the timer makes, the deadline it
arms or that it disarms, outside every lock, before any event of the call that drained the announcement is
dispatched; arming again for the deadline already armed is no move. A timer made without a scheduler arms nothing and
reads no clock, and `weivel_c_timer_now`, where the layer takes the time for a want that needs it, then answers the
caller's mistake.
