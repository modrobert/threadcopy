# threadcopy

### Copyright (C) 2021 by Robert &lt;modrobert@gmail.com&gt;
### Software licensed under Zero Clause BSD.

---

### Description

C program which takes input files and copies the data to given output files using pthreads with verify. The goal is to provide reliable verification with byte-for-byte comparison and good performance while maintaining a low memory footprint, in other words; lean and mean.

---

### Usage

<pre>
$ threadcopy -h
threadcopy v0.16 by modrobert@gmail.com in 2021
Function: Copy input files to given output files using pthreads.
Syntax  : threadcopy [-d] [-h] -i &lt;input file1[|file2|...]&gt;
          -o &lt;output file1[|file2|...]&gt; [-q] [-v]
Options : -d debug enable
          -i input file(s) in order related to output files
          -o output files(s) in order related to input files
          -q quiet flag, only errors reported
          -v for file verification using byte-for-byte comparison
Result  : 0 = ok, 1 = read error, 2 = write error,
          3 = verify error, 4 = arg error.
</pre>

---

### Build

Use 'make' or compile manually with:
<pre>
gcc -O2 -Wpedantic -pthread threadcopy.c -o threadcopy
</pre>

---

### Test commands for bash in Linux

<pre>
# Create test files with random data:
for i in {0..999}; do dd if=/dev/urandom of=in_file$i.bin bs=10M count=1; echo "File number: $i"; done

# Create file arguments:
$( for i in {0..999}; do echo -n "in_file$i.bin|"; done; echo -e "\b " )
$( for i in {0..999}; do echo -n "out_file$i.bin|"; done; echo -e "\b " )

# Command line to copy:
./threadcopy -d -v -i $( for i in {0..999}; do echo -n "in_file$i.bin|"; done; echo -e "\b " ) -o $( for i in {0..999}; do echo -n "out_file$i.bin|"; done; echo -e "\b " )

# File compare, check integrity:
for i in {0..999}; do cmp in_file$i.bin out_file$i.bin; echo "^^ Compared file: $i"; done

# Using 'cp' for comparison runtime:
time for i in {0..999}; do cp in_file$i.bin out_cp_file$i.bin; echo "^^ Copied file: $i"; done

# Clean output files:
rm out_cp_file* ; rm out_file*
</pre>

