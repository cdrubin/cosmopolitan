#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>


int main(int argc, char **argv)
{


	// check value of first parameter
	// if it matches the name of an executable at /zip/[executable]
	// then run it passing all the rest of the parameters to it
	// also think about how to handle if this file was executed 
	// from a symlink with the name of that executable

    fprintf(stderr, "1\n");
	int length = snprintf( NULL, 0, "/zip/%s", argv[1] );
    fprintf(stderr, "2\n");
    char possible_executable_name[ length + 1 ];
    fprintf(stderr, "3\n");
    
	snprintf( possible_executable_name, length + 1, "/zip/%s", argv[1] );
    fprintf(stderr, "4\n");

    fprintf(stderr, "%s\n", possible_executable_name);

	if ( access( possible_executable_name, F_OK ) == 0 ) {
    	fprintf(stderr, "possible executable name found!\n");
		int ret = execv( possible_executable_name, &argv[1] );
		//int ret = execv( "/zip/micro", &argv[1] );
		//int ret = execve("/zip/micro", (char *const[]){0}, (char *const[]){0});

		//fork( possible_executable_name, &argv[1], NULL );
		
    	fprintf(stderr, "why am I here (returned: %d, errno: %d)?\n", ret, errno );
    }
    fprintf(stderr, "6\n");
    exit( -10 );
}
