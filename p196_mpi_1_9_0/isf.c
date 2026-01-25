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
#define MY_READ_WRITE  (_S_IREAD | _S_IWRITE)
#else
#include <unistd.h>
#define MY_READ_WRITE (S_IRUSR | S_IWUSR)
#endif

#if defined(_WIN32) && defined(_MSC_VER)
#define __PRETTY_FUNCTION__ __FUNCTION__
#endif

#if defined(_WIN32) && defined(_MSC_VER)
#define percentzd "%Id"
#else
#define percentzd "%zd"
#endif

#if defined(_WIN32) && defined(_MSC_VER)
#else
static inline
#endif
unsigned short crc1021(unsigned short in_crc, unsigned char data)
{
  unsigned short out_crc;
  unsigned short x;

  x = ((in_crc>>8) ^ data) & 0xff;
  x ^= x>>4;

  out_crc = (in_crc << 8) ^ (x << 12) ^ (x <<5) ^ x;

  out_crc &= 0xffff;

  return out_crc;
}

int read_isf(const char* filename, size_t *full_size, size_t *start, size_t *step, char* current) {
  int crc;
  size_t i;
  FILE* fd = fopen(filename, "r");
  if (fd == NULL) {
    fprintf(stderr, "Opening '%s' failed with errno = %d\n", filename, errno);
    return -1;
  }
  fscanf(fd, "automatic save #%d\n", &crc);
  fscanf(fd, "Initial value:    " percentzd "\n", start);
  fscanf(fd, "Iteration:        " percentzd "\n", step);
  fscanf(fd, "Number of digits: " percentzd "\n", full_size);
  fprintf(stderr, "%s: found " percentzd " " percentzd " " percentzd "\n", __PRETTY_FUNCTION__, *start, *step, *full_size);
  {
    size_t index;
    char temp[72];
    
    for (index = 0 ; index < *full_size; index += 70) {
      int s = fscanf(fd, "%70s\n", temp);
      for (i = 0 ; temp[i] != '\0' ; i++)
        current[(*full_size)-(index+i+1)] = temp[i] - '0';
    }
  }
  fclose(fd);
  return 0;
}

int write_isf(const char* filename, size_t full_size, size_t start, size_t step, char* current) {
  char *temp = (char*)calloc(full_size + full_size/70 + 1024, 1);
  size_t i;
  size_t offset = 0;
  size_t crc_offset = 0;
  size_t max_first;
  unsigned short crc = 0xFFFF;
  int fd;

  offset += sprintf(temp + offset, "automatic save #00000\n");
  max_first = offset;
  offset += sprintf(temp + offset, "Initial value:    " percentzd "\n", start);
  crc_offset = offset;
  offset += sprintf(temp + offset, "Iteration:        " percentzd "\n", step);
  offset += sprintf(temp + offset, "Number of digits: " percentzd "\n", full_size);
 
  {
    size_t index;
    char temp2[72];
    
    for (index = full_size ; index > 0; /* */) {
      for (i = 0 ; i < 70 && index > 0 ; i++, index --)
        temp2[i] = current[index-1] + '0';
      temp2[i] = '\n';
      temp2[i+1] = '\0';
      offset += sprintf(temp + offset, "%s", temp2);
    }
  }

  for (i = crc_offset; i < offset ; i++) {
    crc = crc1021(crc, temp[i]);
  }

  sprintf(temp, "automatic save #%05d", crc);
  temp[max_first-1] = '\n';

  fd = open(filename, O_WRONLY | O_CREAT, MY_READ_WRITE);
  if (fd == -1) {
    fprintf(stderr, "open: errno = %d\n", errno);
    free(temp);
    return -1;
  }
  write(fd, temp, offset);
  close(fd);
  free(temp);

  return 0;
}
