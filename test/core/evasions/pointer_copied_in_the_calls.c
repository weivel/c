static weivel_status evade(const weivel_c_core_table *table, weivel_core *core, weivel_handle plugin,
                            uint32_t *pending_events) {
    weivel_status (*copied)(weivel_core *, weivel_handle, uint32_t *) = table->release_plugin;
    return copied(core, plugin, pending_events);
}
