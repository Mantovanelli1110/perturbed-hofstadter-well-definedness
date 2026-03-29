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
│
├── automaton/
│   ├── README.md
│   ├── branch_comparison_solver.c
│   └── two_mode_automaton.c
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
├── data_generation/
│   ├── README.md
│   └── q_D_macro_profiles_stream_occ_win64.c
│
├── check_local_context_closure_corepsi_win64.c
├── checkall.cpp
├── compress_edges_to_9state.c
├── construct_and_propagate.c
├── infer_pi_from_exact_symbolic_trace_win64.c
└── make_exact_anchor_pairs_win64.c
```

---

## 🔁 Full Verification Pipeline

```text
symbolic_trace.txt
   ↓
(make_exact_anchor_pairs)
   ↓
exact_anchor_pairs.csv
   ↓
(infer_pi)
   ↓
context → symbolic states
   ↓
(q_D_macro_profiles + edge data)
   ↓
(compress_edges)
   ↓
compressed automaton (9-state system)
   ↓
(construct_and_propagate)
   ↓
Λ_adm, Ψ, support sets
   ↓
(check_local_context_closure)
   ↓
model consistency verified
   ↓
(checkall)
   ↓
critical core verification (15 subsets)
```

---

## 🧪 Programs (Root Directory)

### 1. `construct_and_propagate.c`

Core reconstruction and propagation engine.

**Functionality:**

* Builds admissible contexts $\Lambda_{\mathrm{adm}}$
* Constructs compatibility relation $\Psi$
* Builds symbolic transition system
* Enforces **debt rigidity**
* Performs **constraint propagation**
* Computes support sets
* Extracts the critical core structure

**Mathematical role:**

Supports Sections 2–8 of the paper.

---

### 2. `checkall.cpp`

Final verification step.

**Functionality:**

* Enumerates all subsets of
  $$
  \Lambda_{\mathrm{crit}} = {\lambda_0, \lambda_2, \lambda_{14}, \lambda_{24}}
  $$
* Checks existence of valid assignments
* Verifies that no obstruction exists

**Corresponds to:** Section 11.

---

### 3. `check_local_context_closure_corepsi_win64.c`

Consistency verification between:

* local symbolic model
* compressed automaton

**Checks:**

* If $\lambda \to \mu$ is valid, then
  $$
  \pi(\lambda) \to \pi(\mu)
  $$
  is a valid symbolic transition

**Role:**

Ensures that the symbolic model is faithful and introduces no spurious transitions.

---

### 4. `compress_edges_to_9state.c`

Constructs the compressed symbolic automaton.

**Input:**

* `data/D_macro_profiles_34.csv`
* `data/D_debt_edges_34.csv`

**Output:**

* `real_edges.txt`
* `real_edges.json`

Defines the finite transition system used in the proof.

---

### 5. `make_exact_anchor_pairs_win64.c`

Extracts canonical anchor pairs from the symbolic trace.

**Functionality:**

* Reads `symbolic_trace.txt`
* Identifies structural anchor positions
* Outputs alignment pairs

**Output:**

* `exact_anchor_pairs.csv`

**Role:**

Provides the structural backbone for reconstructing local contexts.

---

### 6. `infer_pi_from_exact_symbolic_trace_win64.c`

Infers the projection

$$
\pi : \Lambda_{\mathrm{adm}} \to {S_i[d]}
$$

from the symbolic trace.

**Functionality:**

* Reads symbolic trace + anchor data
* Assigns symbolic states to contexts
* Produces context → state mapping

**Role:**

Connects the context-level system to the compressed automaton.

---

## 🧪 Programs (Subdirectories)

### `data_generation/`

#### `q_D_macro_profiles_stream_occ_win64.c`

Generates macro-profile occurrence data from the symbolic sequence.

Used as input for edge construction and compression.

---

### `automaton/`

* `two_mode_automaton.c`
  Implements the two-mode structure (Mode A / Mode B)

* `branch_comparison_solver.c`
  Auxiliary solver for comparing symbolic branches

---

## ▶️ Reproducibility Guide

### Step 1 — Generate macro profiles

```bash
gcc -O2 -std=c11 -o gen data_generation/q_D_macro_profiles_stream_occ_win64.c
gen
```

---

### Step 2 — Extract anchor pairs

```bash
gcc -O2 -std=c11 -o anchors make_exact_anchor_pairs_win64.c
anchors
```

---

### Step 3 — Infer projection

```bash
gcc -O2 -std=c11 -o infer infer_pi_from_exact_symbolic_trace_win64.c
infer
```

---

### Step 4 — Compress automaton

```bash
gcc -O2 -std=c11 -o compress compress_edges_to_9state.c
compress
```

---

### Step 5 — Build symbolic system

```bash
gcc -O2 -std=c11 -o propagate construct_and_propagate.c
propagate
```

---

### Step 6 — Verify closure

```bash
gcc -O2 -std=c11 -o closure check_local_context_closure_corepsi_win64.c
closure
```

---

### Step 7 — Final verification

```bash
g++ -O2 -std=c++17 -o checkall checkall.cpp
checkall
```

---

## 📊 Guarantees

* All computations are **finite**
* No randomness or heuristics
* Fully deterministic pipeline
* All data reproducible from source

This repository provides a:

> ✅ **machine-checkable certificate of correctness**

---

## 📄 Relation to the Paper

| Paper Section              | Code                                          |
| -------------------------- | --------------------------------------------- |
| Finite symbolic model      | `construct_and_propagate.c`                   |
| Compatibility graph $\Psi$ | `construct_and_propagate.c`                   |
| Debt rigidity              | `construct_and_propagate.c`                   |
| Constraint propagation     | `construct_and_propagate.c`                   |
| Closure verification       | `check_local_context_closure_corepsi_win64.c` |
| Critical core verification | `checkall.cpp`                                |

---

## 📌 Final Remark

This repository demonstrates that a highly nonlocal recursion can be reduced to a **finite-state system whose consistency can be verified by exhaustive computation**.


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


