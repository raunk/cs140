/*
 * This program will add all of the numbers
 * passed as command line input.
 *
 * add 1 2 3
 *
 * should print 6
 */

#include <stdio.h>
#include <stdlib.h>

int
main(int argc, char* argv[])
{
  printf("Num args %d\n", argc);

  int sum = 0;
  int i;
  for(i = 1; i < argc; i++)
  {
    sum += atoi(argv[i]);
  } 

  printf("Sum %d\n", sum);
 
  return 0;
}
