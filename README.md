# MyPinTool - Memory Access Tracer

## 📌 Objective

This project implements a Pintool using Intel PIN to analyze the memory behavior of programs.
It records all memory load and store instructions during execution.

## ⚙️ Features

* Detects memory instructions (Read/Write)
* Records:

  * Program Counter (PC)
  * Access type (R/W)
  * Memory Address
* Limits number of recorded accesses to avoid large output files

## 🛠️ Build Instructions

```bash
cd source/tools/MyPinTool
make
```

## ▶️ Run Instructions

```bash
cd pin_kit
./pin -t source/tools/MyPinTool/obj-intel64/MyPinTool.so -o trace.txt -- ./test/gemm
```

## 📂 Output Format

```
PC, R/W, Memory Address
```

Example:

```
0x4005f6, R, 0x7ffd1234
0x4005fa, W, 0x7ffd1238
```

## ⚠️ Notes

* Output size is limited using a threshold
* Works on any executable (e.g., `/bin/ls`)

## 👨‍💻 Author

Ankit Mittal
