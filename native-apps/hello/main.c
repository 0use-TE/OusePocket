// SPDX-License-Identifier: MIT
#include "ouse_native_api.h"
OUSE_NATIVE_APP();

// The loader uses a normal int(int,char **) entry, without C runtime startup.
int app_main(int argc, char **argv)
{
    (void)argc;(void)argv;
    const ouse_native_api_t *host=ouse_native_get_api(OUSE_NATIVE_ABI);
    if(!host || host->size<sizeof(*host))return 1;
    host->log("Hello from an independently compiled ELF in PSRAM");
    // Exercise loaded .data/.bss, relative relocation and imported callbacks.
    static volatile int32_t counter;
    static const char message[]="Native application finished";
    static const char *volatile message_pointer=message;
    for(counter=0;counter<100;++counter){
        if(host->should_stop()){
            host->log("Native application cooperatively stopped");
            host->report(-1);
            return 0;
        }
        host->report(counter);
        host->sleep_ms(50);
    }
    host->report(counter);
    host->log(message_pointer);
    return 0;
}
