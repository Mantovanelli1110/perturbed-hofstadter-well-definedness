# perturbed-hofstadter-well-definedness

This repository contains the full computational verification pipeline accompanying the paper:

> **A Finite-State Proof of the Well-Definedness of a Perturbed Hofstadter Sequence**

---

## 📌 Overview

We study the perturbed Hofstadter-type recursion

$$
Q(n) = Q(n - Q(n-1)) + Q(n - Q(n-2)) + (-1)^n
$$

with initial values $Q(1) = Q(2) = 1$.

The paper proves that this recursion is **well-defined for all $n \ge 1$**.

---

## 🧠 Key Idea of the Proof

The infinite recursion is reduced to a **finite combinatorial system**:

1. Encode local configurations as **contexts**
2. Construct a finite **compatibility graph** $\Psi$
3. Reduce global consistency to a **constraint satisfaction problem**
4. Show that all obstructions reduce to a **critical core of 4 contexts**
5. Exhaustively verify all **15 subsets** of this core

---

## ⚙️ Repository Structure

```text
perturbed-hofstadter-well-definedness/
│
├── README.md
├── LICENSE
│
├── src/
│   ├── pipeline/
│   │   ├── make_exact_anchor_pairs_win64.c
│   │   ├── infer_pi_from_exact_symbolic_trace_win64.c
│   │   ├── compress_edges_to_9state.c
│   │   └── construct_and_propagate.c
│   │
│   ├── automaton/
│   │   ├── two_mode_automaton.c
│   │   └── branch_comparison_solver.c
│   │
│   ├── verification/
│   │   └── checkall.cpp
│   │
│   └── data_generation/
│       └── q_D_macro_profiles_stream_occ_win64.c
│
├── data/
│   ├── symbolic_trace.txt
│   ├── exact_anchor_pairs.csv
│   ├── D_macro_profiles_34.csv
│   ├── D_macro_profiles_36.csv
│   ├── D_debt_edges_34.csv
│   ├── D_debt_edges_36.csv
│   └── automaton/
│
├── docs/
│   ├── paper.pdf
│   └── reproducibility_guide.md
│
└── output/
    └── .gitkeep
```

---

## 🔁 Full Verification Pipeline

```text
data/symbolic_trace.txt
   ↓
(make_exact_anchor_pairs)
   ↓
data/exact_anchor_pairs.csv
   ↓
(infer_pi)
   ↓
(no global π exists)
   ↓
(data_generation)
   ↓
macro profiles + debt transitions
   ↓
(compress_edges)
   ↓
9-state system (37 edges)
   ↓
(construct_and_propagate)
   ↓
Λ_adm, Ψ, domains
   ↓
(two_mode_automaton)
   ↓
Mode A / Mode B
   ↓
(branch_comparison_solver)
   ↓
independent validation
   ↓
(check_local_context_closure)
   ↓
closure consistency
   ↓
(checkall)
   ↓
critical core verification (15 subsets)
```

---

## 🧪 Programs

### 🔹 Pipeline (`src/pipeline/`)

#### `make_exact_anchor_pairs_win64.c`

Extracts canonical anchor pairs from the symbolic trace.

#### `infer_pi_from_exact_symbolic_trace_win64.c`

Attempts to construct a global projection
$$
\pi : \Lambda_{\mathrm{adm}} \to {S_i[d]}
$$

**Key result:**

```
RESULT: no exact anchored pi exists.
```

---

#### `compress_edges_to_9state.c`

Builds the finite compressed system:

* 9 states
* 37 transitions

---

#### `construct_and_propagate.c`

Core engine:

* Constructs $\Lambda_{\mathrm{adm}}$
* Builds $\Psi$
* Enforces debt rigidity
* Performs constraint propagation
* Computes support sets
* Extracts the critical core

---

#### `check_local_context_closure_corepsi_win64.c`

Verifies consistency between:

* symbolic system
* compressed automaton

---

### 🔹 Automaton (`src/automaton/`)

#### `two_mode_automaton.c`

Constructs the **two-mode structure**:

* Mode A
* Mode B

Provides global consistent assignments.

---

#### `branch_comparison_solver.c`

Independent validation:

* Reconstructs both modes
* Confirms consistency independently

Acts as a **redundant verification layer**.

---

### 🔹 Verification (`src/verification/`)

#### `checkall.cpp`

Final exhaustive verification:

* Checks all subsets of
  $$
  \Lambda_{\mathrm{crit}} = {\lambda_0, \lambda_2, \lambda_{14}, \lambda_{24}}
  $$
* Verifies absence of obstructions

---

### 🔹 Data Generation (`src/data_generation/`)

#### `q_D_macro_profiles_stream_occ_win64.c`

Generates:

* macro profiles
* debt transitions
* occurrence statistics

---

## ▶️ Reproducibility Guide

All commands are executed from the repository root.

---

### 1. Generate macro profiles

```bash
gcc -O2 -std=c11 -o gen src/data_generation/q_D_macro_profiles_stream_occ_win64.c
./gen
```

---

### 2. Construct anchor pairs

```bash
gcc -O2 -std=c11 -o anchors src/pipeline/make_exact_anchor_pairs_win64.c
./anchors
```

---

### 3. Attempt global solution

```bash
gcc -O2 -std=c11 -o infer src/pipeline/infer_pi_from_exact_symbolic_trace_win64.c
./infer
```

Expected:

```
RESULT: no exact anchored pi exists.
```

---

### 4. Compress system

```bash
gcc -O2 -std=c11 -o compress src/pipeline/compress_edges_to_9state.c
./compress
```

---

### 5. Build local system

```bash
gcc -O2 -std=c11 -o propagate src/pipeline/construct_and_propagate.c
./propagate
```

---

### 6. Construct two-mode automaton

```bash
gcc -O2 -std=c11 -o two_mode src/automaton/two_mode_automaton.c
./two_mode
```

---

### 7. Independent branch validation

```bash
gcc -O2 -std=c11 -o branch src/automaton/branch_comparison_solver.c
./branch
```

---

### 8. Check closure

```bash
gcc -O2 -std=c11 -o closure src/pipeline/check_local_context_closure_corepsi_win64.c
./closure
```

---

### 9. Final verification

```bash
g++ -O2 -std=c++17 -o checkall src/verification/checkall.cpp
./checkall
```

Expected:

```
VERIFIED: no simultaneous obstruction on the critical core.
```

---

## 📊 Guarantees

* All structures are **finite and explicitly enumerated**
* Fully deterministic (no randomness)
* Complete pipeline reproducibility
* Independent validation of key structures (two-mode system)

This repository provides a:

> ✅ **machine-checkable certificate of correctness**

---

## 📄 Relation to the Paper

| Paper Section              | Code                                                       |
| -------------------------- | ---------------------------------------------------------- |
| Finite symbolic model      | `src/pipeline/construct_and_propagate.c`                   |
| Compatibility graph $\Psi$ | `src/pipeline/construct_and_propagate.c`                   |
| Two-mode structure         | `src/automaton/two_mode_automaton.c`                       |
| Independent validation     | `src/automaton/branch_comparison_solver.c`                 |
| Closure verification       | `src/pipeline/check_local_context_closure_corepsi_win64.c` |
| Critical core verification | `src/verification/checkall.cpp`                            |

---

## 📌 Final Remark

This repository demonstrates that a highly nonlocal recursion can be reduced to a **finite-state system whose consistency can be verified by exhaustive computation**. All admissible configurations are explicitly generated and checked.


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


