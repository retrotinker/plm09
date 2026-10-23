/* plmc: PL/M (tiny core) to 6809 lwasm compiler -- driver */
#include <stdio.h>
#include <string.h>

static void usage(FILE *f)
{
    fprintf(f, "usage: plmc [-o out.asm] [--org N] [--vectors] [--tokens|--ast] file.plm\n");
}

int main(int argc, char **argv)
{
    if (argc == 2 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
        usage(stdout);
        return 0;
    }
    usage(stderr);
    fprintf(stderr, "plmc: not implemented yet\n");
    return 2;
}
