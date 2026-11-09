/* Dynamically loaded and unloaded by the resolver regression test. */
static __thread unsigned value;
unsigned *tls_fixture_addr(void) { return &value; }
