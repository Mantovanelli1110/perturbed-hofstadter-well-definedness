# perturbed-hofstadter-well-definedness

This repository contains the full computational verification pipeline accompanying the paper:

> **A Finite-State Proof of the Well-Definedness of a Perturbed Hofstadter Sequence**

---

## 📌 Overview

We study the perturbed Hofstadter-type recursion

[
Q(n) = Q(n - Q(n-1)) + Q(n - Q(n-2)) + (-1)^n
]

with initial values (Q(1) = Q(2) = 1).

The paper proves that this recursion is **well-defined for all (n \ge 1)**.

---

## 🧠 Key Idea of the Proof

The infinite recursion is reduced to a **finite combinatorial system**:

1. Encode local configurations as **contexts**
2. Construct a finite **compatibility graph** (\Psi)
3. Reduce global consistency to a **constraint satisfaction problem**
4. Show that all obstructions reduce to a **critical core of 4 contexts**
5. Exhaustively verify all **15 subsets** of this core

---

## ⚙️ Repository Structure

```
automaton/        → symbolic automaton + solvers
data/             → generated datasets (CSV, traces)
data_generation/  → programs that build the finite model
```

---

## 🔁 Full Verification Pipeline

```
symbolic trace
   ↓
macro profiles + edges
   ↓
compressed automaton (9-state system)
   ↓
local context system (Λ_adm, Ψ)
   ↓
constraint propagation
   ↓
critical core extraction
   ↓
finite verification (15 subsets)
```

---

## 🧪 Programs

### 1. `construct_and_propagate.c`

Core reconstruction + propagation engine.

**Functionality:**

* Reconstructs symbolic trace structure
* Builds contexts (\Lambda_{\mathrm{adm}})
* Computes compatibility graph (\Psi)
* Constructs symbolic transition system
* Enforces **debt rigidity**
* Runs **constraint propagation**
* Verifies domain consistency
* Extracts the **critical core structure**

This program justifies Sections 2–8 of the paper.

---

### 2. `checkall_en.cpp`

Final verification of the proof.

**Functionality:**

* Enumerates all 15 subsets of the critical core
* Checks existence of valid assignments
* Confirms:

> **No obstruction exists**

This corresponds to Section 11 of the paper.

---

### 3. `check_local_context_closure_corepsi.c`

Consistency verification between:

* local symbolic system
* compressed transition system

**Checks:**

* every context transition respects observed transitions
* projection (\pi) is consistent

Ensures that the symbolic model introduces no spurious behavior.

---

### 4. `compress_edges_to_9state.c`

Builds the compressed automaton.

**Input:**

* `data/D_macro_profiles_34.csv`
* `data/D_debt_edges_34.csv`

**Output:**

* `real_edges.txt`
* `real_edges.json`

Defines the finite transition system used throughout the proof.

---

## ▶️ Reproducibility Guide

### Step 1 — Generate data

```bash
gcc -O2 -std=c11 -o gen data_generation/q_D_macro_profiles_stream_occ_win64.c
./gen
```

---

### Step 2 — Compress transitions

```bash
gcc -O2 -std=c11 -o compress data_generation/compress_edges_to_9state.c
./compress
```

---

### Step 3 — Build symbolic system

```bash
gcc -O2 -std=c11 -o propagate data_generation/construct_and_propagate.c
./propagate
```

---

### Step 4 — Verify closure

```bash
gcc -O2 -std=c11 -o closure data_generation/check_local_context_closure_corepsi_win64.c
./closure
```

---

### Step 5 — Final verification

```bash
g++ -O2 -std=c++17 -o checkall data_generation/checkall.cpp
./checkall
```

---

## 📊 Guarantees

* All computations are **finite**
* No randomness or heuristics are used
* Fully deterministic pipeline
* All data is reproducible from source

This repository provides a:

> ✅ **machine-checkable certificate of correctness**

for the finite combinatorial part of the proof.

---

## ⚠️ Platform Notes

* Some programs use Windows-specific file handling (`_win64`)
* They can be adapted to Linux with minor changes
* Core logic is platform-independent

---

## 📄 Relation to the Paper

| Paper Section              | Code                                    |
| -------------------------- | --------------------------------------- |
| Finite symbolic model      | `construct_and_propagate.c`             |
| Compatibility graph (\Psi) | `construct_and_propagate.c`             |
| Debt rigidity              | `construct_and_propagate.c`             |
| Constraint propagation     | `construct_and_propagate.c`             |
| Closure verification       | `check_local_context_closure_corepsi.c` |
| Critical core verification | `checkall_en.cpp`                       |

---

## 📌 Final Remark

The repository demonstrates that a highly nonlocal recursion can be reduced to a **finite-state system whose consistency is decidable by exhaustive computation**.

---

## 📜 License

MIT License

Copyright (c) 2026 Marco Mantovanelli

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.


