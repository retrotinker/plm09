/* sim6809: headless MC6809 simulator for plm09 tests -- driver */
#include <stdio.h>
#include <string.h>

static void usage(FILE *f)
{
    fprintf(f, "usage: sim6809 [-t] [-n maxinsns] [--io BASE] [--zero] prog.s19\n");
}

int main(int argc, char **argv)
{
    if (argc == 2 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
        usage(stdout);
        return 0;
    }
    usage(stderr);
    fprintf(stderr, "sim6809: not implemented yet\n");
    return 2;
}
