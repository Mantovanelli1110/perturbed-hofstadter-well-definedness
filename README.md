# perturbed-hofstadter-well-definedness
This repository contains the standalone verification code accompanying the paper “A Finite-State Proof of the Well-Definedness of a Perturbed Hofstadter Sequence”.
## Overview

The paper proves that the perturbed Hofstadter-type recursion

\[
Q(n) = Q(n - Q(n-1)) + Q(n - Q(n-2)) + (-1)^n
\]

with initial values \(Q(1) = Q(2) = 1\) is well-defined for all \(n \ge 1\).

The proof reduces the infinite recursion to a **finite constraint system**, and ultimately to a **finite verification problem on a critical core of four contexts**.

---
## Computational Verification

This repository contains two complementary programs that together provide
a computational certificate for the finite-state reduction developed in the paper.

### 1. `construct_and_propagate.c`

This program reconstructs the finite symbolic system from a concrete symbolic trace
and verifies its internal consistency via local propagation.

It performs the following steps:

- Parses an embedded symbolic trace of the sequence
- Reconstructs the symbolic decomposition into regimes \(R_1, R_2, R_3\)
- Enumerates all admissible local contexts \(\Lambda_{\mathrm{adm}}\)
- Constructs the compatibility relation \(\Psi\)
- Builds the compressed symbolic transition system
- Initializes domains according to the debt constraint
- Enforces **debt rigidity** (all debt-2 contexts forced to \(S6[2]\))
- Runs **iterated local consistency propagation** (constraint filtering)
- Verifies that all domains remain nonempty
- Extracts the induced compatibility structure on the critical core

This program provides a computational justification for:

- the correctness of the context set \(\Lambda_{\mathrm{adm}}\)
- the compatibility relation \(\Psi\)
- the structure of the symbolic transition system
- the rigidity of the debt-2 sector (Section 6)
- the propagation behavior used in the reduction (Sections 7–10)

---

### 2. `checkall_en.cpp`

This program verifies the final finite step of the proof.

It:

- Enumerates all 15 nonempty subsets of the critical core  
  \[
  \Lambda_{\mathrm{crit}} = \{\lambda_0, \lambda_2, \lambda_{14}, \lambda_{24}\}
  \]
- Checks for each subset whether a valid assignment exists
- Uses the precomputed support sets and compatibility edges
- Confirms that **no subset is an obstruction**

This establishes the final step:

> The finite constraint system has no obstruction.

---
### 3. `check_local_context_closure_corepsi.c`

This program verifies the consistency between the local symbolic model
and the empirically observed compressed transition system.

It performs the following:

- Constructs the full set of admissible local contexts \(\Lambda_{\mathrm{adm}}\)
- Builds the local successor relation \(\Psi\)
- Loads experimentally observed compressed states and transitions (CSV)
- Defines a projection
  \[
  \pi : \Lambda_{\mathrm{adm}} \to \{S_i[d]\}
  \]
- Verifies that every local transition is mapped to a valid observed transition

In particular, it checks the **closure property**:

> If \( \lambda \to \lambda' \) is a valid local step,  
> then \( \pi(\lambda) \to \pi(\lambda') \) must be an observed edge.

---

### Mathematical role

This program provides evidence that:

- the symbolic local model does not introduce spurious transitions
- the projection \(\pi\) is compatible with the observed automaton
- the finite reduction faithfully represents the underlying dynamics

---

### Important note

The projection \(\pi\) implemented here is currently a **deterministic symbolic proxy**.

It is sufficient for verification of closure properties, but is not claimed
to be the unique or canonical compression map.

## Logical Dependency

The computational verification mirrors the logical structure of the proof:

| Mathematical Step | Code |
|------------------|------|
| Construction of contexts | `construct_and_propagate.c` |
| Construction of \(\Psi\) | `construct_and_propagate.c` |
| Debt rigidity | `construct_and_propagate.c` |
| Support sets (implicit via propagation) | `construct_and_propagate.c` |
| Reduction to critical core | paper (certified by structure) |
| Final obstruction check | `checkall_en.cpp` |

---

## Scope and Guarantees

- All computations are **finite and deterministic**
- No randomness or heuristics are used
- All data used by the programs is explicitly included in the source code
- The programs can be recompiled and rerun independently

These programs together provide a **machine-checkable certificate**
for the finite combinatorial part of the proof.

---

## ▶️ Compilation

### Local propagator

```bash
gcc -O2 -std=c11 -o propagate construct_and_propagate.c
./propagate
```

### Check all

Compile with:

```bash
g++ -O2 -std=c++17 -o checkall checkall_en.cpp
```

