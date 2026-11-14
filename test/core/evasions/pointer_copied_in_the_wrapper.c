static weivel_status evade(weivel_c_core *core, weivel_handle handle, weivel_refusal *refusal) {
    weivel_status (*copied)(weivel_core *, weivel_handle, weivel_refusal *, uint32_t *) = core->table.unregister;
    uint32_t pending_events = 0;
    return copied(core->core, handle, refusal, &pending_events);
}
