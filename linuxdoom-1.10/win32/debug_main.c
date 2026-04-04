// Debug wrapper to catch crashes

#include <windows.h>
#include <stdio.h>

int mainCRTStartup(void)
{
    // Create console for debugging
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);

    printf("DOOM starting...\n");
    fflush(stdout);

    // Call real main
    extern int main(int argc, char **argv);
    return main(__argc, __argv);
}