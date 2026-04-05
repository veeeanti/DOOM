#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Mock M_CheckParm function for testing
int myargc;
char **myargv;

int M_CheckParm(char* check) {
    for (int i = 1; i < myargc; i++) {
        if (!strcmp(myargv[i], check)) {
            return i;
        }
    }
    return 0;
}

// Test variables that would be set by argument parsing
int nomonsters = 0;
int respawnparm = 0;
int fastparm = 0;
int devparm = 0;
int deathmatch = 0;

int main(int argc, char *argv[]) {
    myargc = argc;
    myargv = argv;
    
    printf("Initial args: ");
    for (int i = 0; i < myargc; i++) {
        printf("%s ", myargv[i]);
    }
    printf("\n");
    
    // Parse standard arguments first (simulating what happens before -args)
    nomonsters = M_CheckParm("-nomonsters");
    respawnparm = M_CheckParm("-respawn");
    fastparm = M_CheckParm("-fast");
    devparm = M_CheckParm("-devparm");
    if (M_CheckParm("-altdeath"))
        deathmatch = 2;
    else if (M_CheckParm("-deathmatch"))
        deathmatch = 1;
    
    printf("After standard parsing:\n");
    printf("  nomonsters: %d\n", nomonsters);
    printf("  respawnparm: %d\n", respawnparm);
    printf("  fastparm: %d\n", fastparm);
    printf("  devparm: %d\n", devparm);
    printf("  deathmatch: %d\n", deathmatch);
    
    // Parse custom launch arguments and process them after standard arguments
    // Find all instances of -args and collect the following arguments
    int args_count = 0;
    char **args_to_process = NULL;
    
    for (;;)
    {
        int p = M_CheckParm("-args");
        if (!p || p >= myargc-1)
            break;  // No more -args, or -args is last argument (nothing to process)
        
        // Count how many arguments follow this -args (until next -args or end)
        int num_following_args = 0;
        while (p + 1 + num_following_args < myargc && 
               strcmp(myargv[p + 1 + num_following_args], "-args") != 0)
        {
            num_following_args++;
        }
        
        // If we found following arguments, collect them for processing
        if (num_following_args > 0)
        {
            // Resize our array to hold these new arguments
            args_to_process = realloc(args_to_process, sizeof(char*) * (args_count + num_following_args));
            
            // Copy the following arguments
            for (int i = 0; i < num_following_args; i++)
            {
                args_to_process[args_count + i] = myargv[p + 1 + i];
            }
            args_count += num_following_args;
        }
        
        // Remove this -args token and its following arguments by shifting
        int shift_amount = 1 + num_following_args; // -args plus its following args
        for (int i = p; i < myargc - shift_amount; i++)
        {
            myargv[i] = myargv[i + shift_amount];
        }
        myargc -= shift_amount;
    }
    
    // Process the collected arguments as if they were standard DOOM arguments
    // We do this by temporarily extending myargv/myargc and then restoring
    if (args_count > 0)
    {
        // Save original state
        int original_argc = myargc;
        char **original_argv = myargv;
        
        // Extend myargv to hold the additional arguments
        myargv = realloc(myargv, sizeof(char*) * (myargc + args_count));
        
        // Append the collected arguments
        for (int i = 0; i < args_count; i++)
        {
            myargv[myargc + i] = args_to_process[i];
        }
        myargc += args_count;
        
        printf("After inserting args, argc = %d\n", myargc);
        printf("Args now are: ");
        for (int i = 0; i < myargc; i++) {
            printf("%s ", myargv[i]);
        }
        printf("\n");
        
        // Re-process standard arguments with our new extended argument list
        nomonsters = M_CheckParm("-nomonsters");
        respawnparm = M_CheckParm("-respawn");
        fastparm = M_CheckParm("-fast");
        devparm = M_CheckParm("-devparm");
        if (M_CheckParm("-altdeath"))
            deathmatch = 2;
        else if (M_CheckParm("-deathmatch"))
            deathmatch = 1;
        
        // Restore original state
        myargv = original_argv;
        myargc = original_argc;
        
        // Free the collected arguments array
        free(args_to_process);
    }
    
    printf("Final parsed values:\n");
    printf("  nomonsters: %d\n", nomonsters);
    printf("  respawnparm: %d\n", respawnparm);
    printf("  fastparm: %d\n", fastparm);
    printf("  devparm: %d\n", devparm);
    printf("  deathmatch: %d\n", deathmatch);
    
    return 0;
}