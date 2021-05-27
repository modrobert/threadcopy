/* 
 threadcopy - Copy input files to given output files using pthreads.
 Copyright (C) 2021 by Robert <modrobert@gmail.com>

 Permission to use, copy, modify, and/or distribute this software for any
 purpose with or without fee is hereby granted.

 THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 PERFORMANCE OF THIS SOFTWARE.

 Compile with: gcc -O2 -Wpedantic -pthread threadcopy.c -o threadcopy
*/

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <pthread.h>
#include <unistd.h>

/* Command exit codes. */
#define EXIT_OK 0
#define READ_ERROR 1
#define WRITE_ERROR 2
#define VERIFY_ERROR 3
#define ARG_ERROR 4

/* Thread status. */
#define TS_INIT 0
#define TS_RUNNING 1
#define TS_DONE 2
#define TS_CHECKED 3

/* File related. */
#define BLOCKSIZE 4096
#define PATH_MAX 1024
#define FILES_MAX 10000

#define UWAIT 1000

#define DELIMITER "|"

/* Structures. */
typedef struct Filedata
{
  char input_name[PATH_MAX];
  char output_name[PATH_MAX];
  unsigned long int size;
  double time_usec;
  double time_sec;
  int result;
  int status;
  int verify;
} filedata;

/* Prototypes. */
void *copyFile(void *arg);
unsigned int get_filenames(char *farg, char fn[FILES_MAX][PATH_MAX]);
unsigned long int check_file_size(FILE *fp);

/* Macros and globals. */
int pout = 1; /* quiet flag */
#define PRINT(...) if(pout){printf(__VA_ARGS__);}
int dout = 0; /* debug flag */
#define DPRINT(...) if(dout){printf(__VA_ARGS__);}

/* Making sure these are allocated on heap as opposed to stack. */
static char ifiles[FILES_MAX][PATH_MAX];
static char ofiles[FILES_MAX][PATH_MAX];
static unsigned long int sfiles[FILES_MAX][1];


int main(int argc, char *argv[])
{
 const char *PROGTITLE = "threadcopy v0.16 by modrobert@gmail.com in 2021\n";

 FILE *infile;
 int inumf = 0;
 int onumf = 0;
 int bnumf = 0;
 int i, opt;
 int t_result;
 int t_run;
 int t_complete = 0;
 int cmd_result = 0;
 unsigned long int of_max = 0;

 int dflag = 0; /* debug flag */
 int hflag = 0; /* help flag */
 int iflag = 0; /* input files(s) */
 int oflag = 0; /* output file(s) */
 int qflag = 0; /* quiet flag */
 int vflag = 0; /* verify flag */ 
 char *ivalue = "i", *ovalue = "o";

 double t_usec;
 double t_sec;
 struct timeval t1, t2;

 struct rlimit rl;

 /* Clear buffers. */
 for (i = 0; i < FILES_MAX; i++)
 {
  memset(ifiles[i], 0, PATH_MAX);
  memset(ofiles[i], 0, PATH_MAX);
  memset(sfiles[i], 0, sizeof(unsigned long int));
 }

 opterr = 1; /* Turn on getopt '?' error handling. */

 /* Handle arguments. */
 while ((opt = getopt (argc, argv, "dhi:o:qv")) != -1)
 {
  switch (opt)
  {
   case 'd':
    dflag = 1;
    dout = 1;
    break;
   case 'h':
    hflag = 1;
    break;
   case 'i':
    iflag = 1;
    ivalue = optarg;
    break;
   case 'o':
    oflag = 1;
    ovalue = optarg;
    break;
   case 'q':
    qflag = 1;
    pout = 0;
    break;
   case 'v':
    vflag = 1;
    break;
   default: /* '?' */ 
    fprintf(stderr, "Usage: %s -io [-dhqv]\n", argv[0]);
    fprintf(stderr, "Try '%s -h' for more information.\n", argv[0]);
    exit(ARG_ERROR);
  }
 }

 PRINT("%s", PROGTITLE);

 if (argc == 1)
 {
  fprintf(stderr, "Usage: %s -io [-dhqv]\n", argv[0]);
  fprintf(stderr, "Try '%s -h' for more information.\n", argv[0]);
  exit(ARG_ERROR);
 }

 if (hflag)
 {
  PRINT("Function: Copy input files to given output files using pthreads.\n");
  PRINT("Syntax  : threadcopy [-d] [-h] -i <input file1[" DELIMITER
        "file2" DELIMITER "...]>\n"
        "          -o <output file1[" DELIMITER "file2" DELIMITER "...]> "
        "[-q] [-v]\n");
  PRINT("Options : -d debug enable\n");
  PRINT("          -i input file(s) in order related to output files\n");
  PRINT("          -o output files(s) in order related to input files\n");
  PRINT("          -q quiet flag, only errors reported\n");
  PRINT("          -v for file verification using byte-for-byte comparison\n");
  PRINT("Result  : 0 = ok, 1 = read error, 2 = write error,\n"
        "          3 = verify error, 4 = arg error.\n");
  exit(EXIT_OK);
 }

 for (i = optind; i < argc; i++)
  PRINT("Ignoring non-option argument: %s\n", argv[i]);

 /* General sanity checks. */

 /* Check given filenames for dupes and bad combos. */
 if (strcmp(ivalue, ovalue) == 0)
 {
  fprintf(stderr, "Input and output args are same, needs to be unique.\n");
  fprintf(stderr, "Try '%s -h' for more information.\n", argv[0]);
  exit(ARG_ERROR);
 }
 DPRINT("File arguments: -i %s -o %s\n", ivalue, ovalue);
 inumf = get_filenames(ivalue, ifiles);
 onumf = get_filenames(ovalue, ofiles);
 if (inumf != onumf)
 {
  fprintf(stderr, "Input file count %d does not match output file "
                  "count %d.\n", inumf, onumf);
  fprintf(stderr, "Try '%s -h' for more information.\n", argv[0]);
  exit(ARG_ERROR);
 }
 if (dout)
 {
  printf("---------------------------\n");
  for (i = 0; i < inumf; i++)
  {
   printf("i[%04d]: %s  o[%04d]: %s\n", i, &ifiles[i][0], i, &ofiles[i][0]);
  }
   printf("---------------------------\n");
 }
 /* Mark missing files and store size. */
 for (i = 0; i < inumf; i++)
 {
  infile = fopen(ifiles[i], "r");
  if (infile == NULL)
  {
   fprintf(stderr, "Input file not found: %s\n", ifiles[i]);
   fprintf(stderr, "Removing output file: %s\n", ofiles[i]);
   /* Marking missing files with NULL character... */
   ifiles[i][0] = '\0';
   ofiles[i][0] = '\0';
   /* ...and zero size. */
   sfiles[i][0] = 0;
   continue;
  }
  sfiles[i][0] = check_file_size(infile);
  fclose(infile);
 }
 /* Adjusting max open files limit according to files parsed. */
 getrlimit(RLIMIT_NOFILE, &rl);
 of_max = (unsigned long int)inumf + (unsigned long int)onumf;
 if (rl.rlim_cur < of_max)
 {
  if (of_max > rl.rlim_max)
  {
   fprintf(stderr, "The max number of open files is: %lu\n", rl.rlim_max);
   fprintf(stderr, "Run 'ulimit -n %lu' command.\n", of_max);
   exit(READ_ERROR);
  }
  rl.rlim_cur = of_max;
  setrlimit (RLIMIT_NOFILE, &rl);
  DPRINT("Max open files set to: %lu\n", rl.rlim_cur);
 }

 /* Start timer. */
 gettimeofday(&t1, NULL);

 /* Declaring variable thread buffers. */
 pthread_t tid[inumf];
 filedata fdata[inumf];

 /* Clearing variable thread buffers. */
 memset(tid, 0, sizeof tid);
 memset(fdata, 0, sizeof fdata);

 /* Start processing file copy threads. */
 PRINT("Starting thread processing.\n");
 for (i = 0; i < inumf; i++)
 {
  if (ifiles[i][0] == '\0')
  {
   DPRINT("Skipped bad file pair thread: [%04d]\n", i);
   bnumf++;
   continue;
  }
  strncpy(fdata[i].input_name, ifiles[i], PATH_MAX);
  strncpy(fdata[i].output_name, ofiles[i], PATH_MAX);
  fdata[i].size = sfiles[i][0];
  fdata[i].verify = vflag;
  DPRINT("Creating thread [%04d] with file copy: %s -> %s\n",
         i, ifiles[i], ofiles[i]);
  t_result = pthread_create(&tid[i], NULL, copyFile, (void *)&fdata[i]);
  if (t_result != 0)
  {
   fprintf(stderr, "Error creating thread [%4d]: %d\n", i, t_result);
   bnumf++;
  }
  else
  {
   fdata[i].status = TS_RUNNING;
  }
 }

 PRINT("Started %d file copy threads.\n", (inumf - bnumf));

 while (!t_complete)
 {
  t_run = 0;
  for (i = 0; i < inumf; i++)
  {
   if (fdata[i].status == TS_RUNNING)
   {
    t_run++;
   }
   else if (ifiles[i][0] == '\0')
   {
    /* Skipping bad file pair. */
    continue;
   }
   else if (fdata[i].status == TS_DONE)
   {
    if (fdata[i].result == EXIT_OK)
    {
     if (fdata[i].verify)
     {
      DPRINT("Completed thread [%04d] verified OK in %f second(s): %s -> %s\n",
             i, (double)fdata[i].time_sec + (double)fdata[i].time_usec,
             ifiles[i], ofiles[i]);
     }
     else
     {
      DPRINT("Completed thread [%04d] OK in %f second(s): %s -> %s\n",
             i, (double)fdata[i].time_sec + (double)fdata[i].time_usec,
             ifiles[i], ofiles[i]);
     }
    }
    else
    {
     /* TODO: Command exit result code only shows last thread error. */
     cmd_result = fdata[i].result;
    }
    fdata[i].status = TS_CHECKED;
   }
  } /* thread exit for loop */
  if (t_run == 0)
  {
   t_complete = 1;
  }
  /* Sleep for UWAIT micro seconds. */
  usleep(UWAIT);
 } /* thread exit while loop */
 DPRINT("Exit with result: %d\n", cmd_result);
 /* End timer. */
 gettimeofday(&t2, NULL);
 t_usec = (double) (t2.tv_usec - t1.tv_usec) / 1000000;
 t_sec = (double) (t2.tv_sec - t1.tv_sec);
 if (vflag)
 {
  PRINT("All files copied and verified in %f second(s).\n",
        (double)t_sec + (double)t_usec);
 }
 else
 {
  PRINT("All files copied in %f second(s).\n",
        (double)t_sec + (double)t_usec);
 }
 return (cmd_result);
} /* main */


/* Functions. */

void *copyFile(void *arg)
{
 filedata *fdata = (filedata *)arg;

 FILE *infile, *outfile;
 unsigned long int filesize = fdata->size;
 int bytes_read;
 int bytes_write;
 unsigned char ibuffer[BLOCKSIZE];
 unsigned char obuffer[BLOCKSIZE];
 int i;

 struct timeval t1, t2;

 /* Reset time. */
 fdata->time_usec = 0;
 fdata->time_sec = 0;

 /* Start timing thread. */
 gettimeofday(&t1, NULL);

 /* Clear buffers. */
 memset(ibuffer, 0, BLOCKSIZE);
 memset(obuffer, 0, BLOCKSIZE);

 /* Opening files for copy. */
 if ((infile = fopen(fdata->input_name, "r")) == NULL)
 {
  fprintf(stderr, "Error while opening input file: %s\n", fdata->input_name);
  fdata->status = TS_DONE;
  fdata->result = READ_ERROR;
  pthread_exit(NULL);
 }
 if ((outfile = fopen(fdata->output_name, "w")) == NULL)
 {
  fprintf(stderr, "Error while opening output file: %s\n", fdata->output_name);
  fdata->status = TS_DONE;
  fdata->result = WRITE_ERROR;
  fclose(infile);
  pthread_exit(NULL);
 }

 /* Copy file. */
 while (!feof(infile))
 {
  if ((bytes_read = fread(ibuffer, 1, BLOCKSIZE, infile)) <= 0)
  {
   if (feof(infile)) break;
   fprintf(stderr, "Error while reading input file: %s\n", fdata->input_name);
   fdata->status = TS_DONE;
   fdata->result = READ_ERROR;
   fclose(infile);
   fclose(outfile);
   pthread_exit(NULL);
  }
  bytes_write = bytes_read;
  if (fwrite(ibuffer, 1, bytes_write, outfile) != bytes_write)
  {
   fprintf(stderr, "Error while writing output file: %s\n",
           fdata->output_name);
   fdata->status = TS_DONE;
   fdata->result = WRITE_ERROR;
   fclose(infile);
   fclose(outfile);
   pthread_exit(NULL);
  }
 } /* while !feof */
 fclose(infile);
 fclose(outfile);

 if (fdata->verify)
 {
  /* Opening files for verification. */
  if ((infile = fopen(fdata->input_name, "r")) == NULL)
  {
   fprintf(stderr, "Error while opening input file: %s\n", fdata->input_name);
   fdata->status = TS_DONE;
   fdata->result = READ_ERROR;
   pthread_exit(NULL);
  }
  if ((outfile = fopen(fdata->output_name, "r")) == NULL)
  {
   fprintf(stderr, "Error while opening output file: %s\n",
           fdata->output_name);
   fdata->status = TS_DONE;
   fdata->result = READ_ERROR;
   fclose(infile);
   pthread_exit(NULL);
  }

  /* Read input and output files. */
  while (!feof(infile))
  {
   if ((bytes_read = fread(ibuffer, 1, BLOCKSIZE, infile)) <= 0)
   {
    if (feof(infile)) break;
    fprintf(stderr, "Error while reading input file: %s\n", fdata->input_name);
    fdata->status = TS_DONE;
    fdata->result = READ_ERROR;
    fclose(infile);
    fclose(outfile);
    pthread_exit(NULL);
   }
   if (fread(obuffer, 1, bytes_read, outfile) != bytes_read)
   {
    fprintf(stderr, "Error while reading output file: %s\n",
            fdata->output_name);
    fdata->status = TS_DONE;
    fdata->result = READ_ERROR;
    fclose(infile);
    fclose(outfile);
    pthread_exit(NULL);
   }
   /* Compare read buffers. */
   for (i = 0; i < bytes_read; i++)
   {
    if (ibuffer[i] != obuffer[i])
    {
     fprintf(stderr, "Verification failed: %s != %s\n",
             fdata->input_name, fdata->output_name);
     fdata->status = TS_DONE;
     fdata->result = VERIFY_ERROR;
     fclose(infile);
     fclose(outfile);
     pthread_exit(NULL);
    }
   }
  } /* while !feof */
  fclose(infile);
  fclose(outfile);
 } /* if verify */

 /* Setting status. */
 fdata->status = TS_DONE;
 /* Result OK. */
 fdata->result = EXIT_OK;

 /* End timing thread. */
 gettimeofday(&t2, NULL);
 fdata->time_usec = (double) (t2.tv_usec - t1.tv_usec) / 1000000;
 fdata->time_sec = (double) (t2.tv_sec - t1.tv_sec);
} /* copyFile */ 

unsigned int get_filenames(char *farg, char fn[FILES_MAX][PATH_MAX])
{
 char *tstr, *saveptr, *token;
 int i;
 int numf = 0;
 if (strlen(farg) > 0)
 {
   if (strstr(farg, DELIMITER) == NULL)
   {
    strncpy(fn[0], farg, PATH_MAX);
    return 1;
   }
 }
 else
 {
  return 0;
 }
 for (i = 0, tstr = farg; i < FILES_MAX; i++, tstr = NULL)
 {
  token = strtok_r(tstr, DELIMITER, &saveptr);
  if (token == NULL)
  {
   numf = i;
   break;
  }
  strncpy(fn[i], token, PATH_MAX);
 }
 if (numf > 0)
  numf -= 1;
 return numf;
}

unsigned long int check_file_size(FILE *fp)
{
 unsigned long int fsize;
 fseek(fp, 0L, SEEK_END);
 fsize = ftell(fp);
 fseek(fp, 0L, SEEK_SET);
 return fsize;
}

/* vim:ts=1:sw=1:ft=c:et:ai:
*/
