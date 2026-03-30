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
## 📥 Symbolic Trace

The file `data/symbolic_trace.txt` (and the embedded version used in some programs) contains a symbolic encoding of the sequence up to dyadic level (m = 36).

It consists of:

* a **5-state path** describing the local configuration types
* an **E-code sequence** describing the recursive structure

This trace is obtained by direct simulation of the recursion and subsequent symbolic encoding.

It serves as a **fixed deterministic input** for reconstructing the finite model used in the proof.

For convenience and reproducibility, the same trace is embedded directly in certain programs (e.g. construct_and_propagate.c), but it is identical to the version stored in:

data/symbolic_trace.txt

---
## Quick Start

To execute the full formal computational proof pipeline on Windows, run:

```bat
run_all.bat
```

The script stops immediately if any step fails and writes a complete execution log to:

```text
output\run_all.log
```

## ⚙️ Repository Structure

```text
perturbed-hofstadter-well-definedness/
│
├── README.md
├── LICENSE
├── run_all.bat
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
│   ├── data_generation/
│   │    └── q_D_macro_profiles_stream_occ_win64.c
│   │ 
│   └── trace_generation/
│        └── q_dyadic_jT_win64_map.c
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

The formal computational proof pipeline used in the paper consists of the following steps:

```text
data/symbolic_trace.txt
   ↓
(make_exact_anchor_pairs)
   ↓
data/exact_anchor_pairs.csv
   ↓
(data_generation)
   ↓
macro profiles + debt transitions
   ↓
(compress_edges)
   ↓
9-state compressed symbolic system
   ↓
(construct_and_propagate)
   ↓
Λ_adm, Ψ, debt rigidity, propagated domains
   ↓
(two_mode_automaton)
   ↓
Mode A / Mode B structure
   ↓
(branch_comparison_solver)
   ↓
independent validation of the two-mode decomposition
   ↓
(checkall)
   ↓
critical core verification (all 15 nonempty subsets)
```

---

## ▶️ Reproducibility Guide

All commands below are executed from the repository root.

### 1. Construct anchor pairs from the symbolic trace

```bash
gcc -O2 -std=c11 -o anchors src/pipeline/make_exact_anchor_pairs_win64.c
./anchors
```

This step extracts the canonical anchor pairs from the symbolic trace and writes:

```text
data/exact_anchor_pairs.csv
```

---

### 2. Generate macro-profile and debt-transition data

```bash
gcc -O2 -std=c11 -o gen src/data_generation/q_D_macro_profiles_stream_occ_win64.c
./gen
```

This step generates the observed macro profiles and debt-transition data used for the compressed symbolic system.

---

### 3. Compress the observed transition structure to the 9-state system

```bash
gcc -O2 -std=c11 -o compress src/pipeline/compress_edges_to_9state.c
./compress
```

This step constructs the compressed symbolic transition system used throughout the proof.

---

### 4. Construct the finite local model and propagate constraints

```bash
gcc -O2 -std=c11 -o propagate src/pipeline/construct_and_propagate.c
./propagate
```

This is the core reconstruction step. It:

* constructs the admissible context set $\Lambda_{\mathrm{adm}}$
* constructs the compatibility relation $\Psi$
* enforces debt rigidity
* performs local constraint propagation
* extracts the induced critical-core structure

---

### 5. Construct the two-mode automaton

```bash
gcc -O2 -std=c11 -o two_mode src/automaton/two_mode_automaton.c
./two_mode
```

This step computes the two global modes of valid assignments, referred to in the paper as **Mode A** and **Mode B**.

---

### 6. Independently validate the two-mode decomposition

```bash
gcc -O2 -std=c11 -o branch src/automaton/branch_comparison_solver.c
./branch
```

This step provides an independent solver-based verification of the two-mode structure.

---

### 7. Verify the critical core exhaustively

```bash
g++ -O2 -std=c++17 -o checkall src/verification/checkall.cpp
./checkall
```

Expected final output:

```text
VERIFIED: no simultaneous obstruction on the critical core.
```

This is the final finite verification step used in the proof.

---

## 📄 Role of the Pipeline in the Proof

The formal computer-assisted part of the proof depends on the following chain:

1. finite symbolic compression of the observed dynamics,
2. reconstruction of the admissible context graph,
3. derivation of the two-mode structure,
4. independent validation of that two-mode decomposition,
5. exhaustive verification that no obstruction exists on the critical core.

In particular, the final proof certificate is carried by the programs

* `src/pipeline/construct_and_propagate.c`
* `src/automaton/two_mode_automaton.c`
* `src/automaton/branch_comparison_solver.c`
* `src/verification/checkall.cpp`

These are the programs that should be regarded as the **formal proof pipeline** for the finite verification component of the paper.


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
| Critical core verification | `src/verification/checkall.cpp`                            |

---
### Optional: Reconstructing the symbolic trace from first principles

For full transparency, the symbolic trace can be regenerated directly from the defining recursion using:

```bash
gcc -O2 -std=c11 -o trace src/trace_generation/q_dyadic_jT_win64_map.c
trace 
```

This program performs an exact simulation of the perturbed Hofstadter recursion up to dyadic level (m = 36), i.e. up to (n = 2^{36}), using a memory-mapped array.

At each dyadic level, it extracts:

* the derived sequences ((E_m, F_m, G_m)), (\kappa_m), and (\delta_m),
* the induced binary sequence (c_m),
* the corresponding **5-state path** obtained from local 4-blocks,
* the decomposition into **return words**,
* and the resulting **E-code sequence** (with symbols (R1, R2, R3)).

The relevant output for the proof is the block:

```text
Completed dyadic level m = 36

5-state path = {...}

E-code = {...}
```

To reproduce the file used in the pipeline, the output can be redirected:

```bash
trace > data/symbolic_trace_raw.txt
```

The corresponding section is then copied into:

```text
data/symbolic_trace.txt
```

---

**Important.**
This step is **not required** for the verification pipeline. The file `data/symbolic_trace.txt` is treated as a fixed deterministic input.

However, including this program ensures that the symbolic trace is fully reproducible from first principles and not a manually constructed artifact.


## 📌 Final Remark

This repository demonstrates that a highly nonlocal recursion can be reduced to a **finite-state system whose consistency can be verified by exhaustive computation**. All admissible configurations are explicitly generated and checked.


