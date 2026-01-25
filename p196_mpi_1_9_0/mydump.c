/*
  Copyright © 2011-2013 Romain Dolbeau <romain@dolbeau.org>
  
  This file is part of p196_mpi.

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License version 2 as
  published by the Free Software Foundation.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#if defined(_WIN32) && defined(_MSC_VER)
#include <io.h>
#define open(a,b,c) _open(a,b,c)
#define read(a,b,c) _read(a,b,c)
#define write(a,b,c) _write(a,b,c)
#define close(a) _close(a)
#define MY_READ (_S_IREAD)
#else
#include <unistd.h>
#define MY_READ (S_IRUSR)
#endif

#if defined(_WIN32) && defined(_MSC_VER)
#define __PRETTY_FUNCTION__ __FUNCTION__
#endif

#if defined(_WIN32) && defined(_MSC_VER)
#define percentzd "%Id"
#else
#define percentzd "%zd"
#endif

int read_dump(const char* filename, size_t *full_size, size_t *start, size_t *step, char* current) {
  struct stat buf;
  int err;
  int fd;
  size_t s;
  size_t i;
  char *t0;
  err = sscanf(filename, "dump." percentzd "." percentzd "", start, step);
  if ((err == EOF) || (err < 2)) {
    fprintf (stderr, "Wrong file name");
    return -2;
  }
  err = stat(filename, &buf);
  if (err) {
    fprintf(stderr, "stat: err = %d\n", err);
    return -2;
  }
  *full_size = buf.st_size;
  fprintf(stderr, "%s: found " percentzd " " percentzd " " percentzd "\n", __PRETTY_FUNCTION__, *start, *step, *full_size);
  t0 = malloc((*full_size)+1);
  if (!t0) {
    fprintf(stderr, "MALLOC FAILED!\n");
    return -1;
  }
  fd = open(filename, O_RDONLY, MY_READ);
  if (fd == -1) {
    fprintf(stderr, "open: errno = %d\n", errno);
    return -2;
  }
  s = read(fd, t0, buf.st_size);
  if (s != buf.st_size) {
    fprintf(stderr, "read: " percentzd " <> " percentzd "\n", s, buf.st_size);
    return -2;
  }
  close(fd);
  for (i = *full_size ; i > 0 ; i--) {
    current[i-1] = t0[*full_size - i] - '0';
  }
  free(t0);
  return 0;
}

int write_dump(const char* filename, size_t full_size, size_t start, size_t step, char* current) {
  FILE *f;
  size_t i;
  f = fopen(filename, "w");
  if (f == NULL) {
    fprintf(stderr, "fopen: errno = %d\n", errno);
    return -2;
  }
  for (i = full_size; i > 0; --i) /* TODO: slow */
    fprintf(f, "%c", current[i - 1] + '0');
  fclose(f);
  return 0;
}
