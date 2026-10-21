// Injection control only. No UI, hooks, allocation, logging, game reads or API calls.
static volatile unsigned int NSPProbeLoaded;
__attribute__((constructor)) static void NSPProbeEntry(void) {
    NSPProbeLoaded=1;
}
