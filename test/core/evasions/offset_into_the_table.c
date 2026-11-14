typedef weivel_status (*advancing)(weivel_core *core, int64_t now, uint32_t *pending_events);

static weivel_status evade(const weivel_c_core_table *table, weivel_core *core, uint32_t *pending_events) {
    const char *place = (const char *)table + offsetof(weivel_c_core_table, advance);
    return (*(const advancing *)(const void *)place)(core, 0, pending_events);
}
